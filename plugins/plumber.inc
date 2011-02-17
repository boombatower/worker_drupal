<?php
/**
 * @file
 * Provide Drupal plumber test runner plugin.
 *
 * - install
 * - run tests
 * - code coverage (later)
 *
 * The following Drupal variables are expected to be set as shown.
 * - simpletest_clear_results: 0
 * - simpletest_verbose: 0
 *
 * As noted in the code comment found in plumber_results() the 'context'
 * property must be greater than PLUMBER_INCREMENT.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * The maximum number of assertions to process at one time.
 */
define('PLUMBER_INCREMENT', 1000);

/**
 * The maximum number of relevant assertions.
 */
define('PLUMBER_MAX', 1000);

$plugin = array(
  'title' => t('Drupal plumber'),
  'description' => t('Provide Drupal plumber test runner plugin.'),
  'perform' => 'plumber_perform',
);

/**
 * Perform the job.
 *
 * @param $properties
 *   Associative array of properties applicable to the current job.
 *   Supported keys and values are:
 *   - database: The type of database to connect to. A valid plugin with the
 *       same name should exist in the worker/plugins/database directory.
 *   - version: The Drupal primary version number.
 *   - plugin: The name of the plugin to load. A valid plugin with the
 *       same name should exist in the worker-drupal/plugins directory.
 *
 *     Plumber parameters.
 *   - variables: Associative array of Drupal variables to set.
 *   - classes: Array of test class names to run tests on.
 *   - statuses: Array of relevant assertion statuses to store.
 *       Example: array('fail', 'exception', 'debug')
 *       Default: ?
 *   - context: Integer indicating the number of assertions surrounding a
 *      "relevant" assertion to return. Relevant assertions are defined by the
 *      "statuses" key.
 *
 * @return
 *   An array containing a boolean for pass/fail and the result. The result
 *   is a string message or an associative array with the following keys:
 *   log, see worker_execute_concurrent(); results, see plumber_results(); and
 *   meta, see plumber_meta().
 *
 * @see worker_perform()
 */
function plumber_perform(array $properties) {
  // Load common Drupal conduit functions.
  require drupal_get_path('module', 'worker_drupal') . '/worker_drupal.inc';

  // Set the SimpleTest flags to ensure the results are available and verbose
  // files are not needlessly generated.
  $properties['variables']['simpletest_clear_results'] = FALSE;
  $properties['variables']['simpletest_verbose'] = FALSE;

  if (!worker_drupal_install($properties)) {
    return array(FALSE, worker_log_last());
  }

  if (!worker_drupal_module_enable('simpletest', $properties['version'])) {
    return array(FALSE, worker_log_last());
  }

  if (!($log = plumber_plumb($properties))) {
    // Unrecoverable error related to configuration occurred.
    return array(FALSE, worker_log_last());
  }

  // Process the results of the test and parse the test meta data.
  $results = plumber_results($properties);
  $meta = plumber_meta($properties);

  // If there are no fails or exceptions then consider a pass.
  return array($results['#total'][1] + $results['#total'][2] == 0, array(
    'log' => $log,
    'result' => $results,
    'meta' => $meta,
  ));
}

/**
 * Plumb the plumbing (ie. run tests).
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   Associative array keyed by test class name and containing the length of
 *   time, in seconds, that the task took to complete, otherwise FALSE.
 * @see worker_execute_concurrent()
 */
function plumber_plumb(array $properties) {
  // Generate list of commands to run all test cases.
  $commands = array();
  $command = plumber_script() . ' --class ';
  foreach ($properties['classes'] as $class) {
    $commands[$class] = $command . $class;
  }

  // Run tests using manual concurrency.
  worker_log('Run ' . number_format(count($properties['classes'])) . ' test case(s) in ' . number_format(count($commands)) . ' batch(es).');
  worker_chdir();
  $log = worker_execute_concurrent($commands);
  chdir(DRUPAL_ROOT);

  return $log;
}

/**
 * Process the results of running the tests.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   Multi-dimentional, associative array of plumber results keyed by the
 *   test class and containing an array of assertions with the keys: line,
 *   type, message, function, group, and file; and a sum of the number of
 *   assertions of each type. The line key will FALSE if there is no line
 *   associated with the message.
 *
 *   @code
 *     $result = array(
 *       '#total' => array(1, 0, 0),
 *       'TestClass' => array(
 *         '#total' => array(1, 0, 0),
 *         array(
 *           'line' => 17, // number or FALSE
 *           'type' => 'pass', // pass|fail|exception|debug
 *           'message' => 'Massive fail'
 *           'function' => 'TestClass->foo()',
 *           'group' => 'Some Group',
 *           'file' => '/path/to/file.php',
 *         ),
 *       ),
 *     );
 *   @endcode
 */
function plumber_results(array $properties) {
  worker_log('Process results.');

  // Get the worker database so the results can be queried.
  $connection = worker_database('drupal', $properties['database']);

  // Select a summary of the results of each test class.
  $query = $connection->select('simpletest', 's');
  $query->groupBy($query->addField('s', 'test_class'));
  $query->groupBy($query->addField('s', 'status'));
  $query->addExpression('COUNT(message_id)', 'count');
  $result = $query->execute();

  // Map of message type (status) to #total key.
  $map = array_flip(array('pass', 'fail', 'exception'));

  // Build an associative array of summaries keyed by the test class and
  // containing the count of each status type found in the results for the test
  // class. Also compile a list of classes that have status types that are
  // found in the 'statues' property.
  $results = array('#total' => array(0, 0, 0));
  $classes = array();
  while ($summary = $result->fetchAssoc()) {
    if (empty($results[$summary['test_class']])) {
      $results[$summary['test_class']]['#total'] = array(0, 0, 0);
    }
    $type = $map[$summary['status']];
    $results['#total'][$type] += $summary['count'];
    $results[$summary['test_class']]['#total'][$type] = $summary['count'];

    // If there are relevant assertions related to the test class then add the
    // class to the list of classes for  later processing when the actual
    // assertions are loaded and check for relevancy.
    if (in_array($summary['status'], $properties['statuses'])) {
      $classes[$summary['test_class']] = $summary['test_class'];
    }
  }
  worker_log('> ' . number_format(count($classes)) . ' relevant class(es) found.');

  // Calculate the context length ahead of time since it is static. The length
  // includes the amount of context on either side of the relevant assertion.
  $context_length = $properties['context'] * 2 + 1;

  // Cycle through the classes that are known to have assertions with statuses
  // that are to be included in the results.
  $total = 0;
  foreach ($classes as $class) {
    // Clear out the message_id which is used to loop over assertions for a
    // test class to ensure that memory usage stays minimal. Also reset the
    // assertions array so the loop is not confused into thinking it is context
    // from the previous cycle within a test class.
    $message_id = 0;
    $assertions = array();
    do {
      // If there are assertions then this is not the first iteration of the
      // do-while loop so we need to keep enough assertions so there is context
      // in case the first few assertions need to be reported.
      if ($assertions) {
        // Use the message_id from the last assertion processed when making
        // another loop interation.
        $message_id = $assertion['message_id'];

        // In case of test cases or loop interations with very few assertions
        // the count may be lower then the context amount in which case the
        // result would be incorrect.
        $context = min($count, $properties['context']);
        $assertions = array_slice($assertions, -1 * $context, $context, TRUE);
      }

      // Select the next set of assertions.
      $assertions += $connection->select('simpletest')
        ->fields('simpletest')
        ->condition('message_id', $message_id, '>')
        ->condition('test_class', $class)
        ->orderBy('message_id')
        ->range(0, PLUMBER_INCREMENT)
        ->execute()
        ->fetchAllAssoc('message_id', PDO::FETCH_ASSOC);

      // Cycle through each assertion looking for assertions that should be
      // reported and grab the relevant context around the assertion.
      $i = 0;
      // @todo Isn't this code reiterating over assertions previously processed?
      foreach ($assertions as $assertion) {
        if (in_array($assertion['status'], $properties['statuses'])) {
          // Since the $assertions array is keyed by message_id adding an
          // assertion multiple times will not result in duplicate entries. In
          // case the assertion is at the beginning of the list we need to
          // ensure the offset parameter to array_splice() is non-negative.
          $length = $properties['context'] > $i ? $context_length - ($properties['context'] - $i) : $context_length;
          $results[$class] += array_slice($assertions, max($i - $properties['context'], 0), $length, TRUE);

          // If the number of relevant assertions has gone over the maximum
          // number allowed then break out of the processing loops.
          if ($total + count($results[$class]) >= PLUMBER_MAX) {
            break 3;
          }
        }
        $i++;
      }

      // Clean results for the class.
      plumber_results_clean($results[$class]);

      // Store the number of assertions processed in the loop since the value
      // may be used multiple times.
      $count = count($assertions);
    }
    // If the number of assertions returned is the same as or greater than the
    // increment amount, due to context from previous loop interation, then
    // there is a possibility for more assertions to exist. This assumes the
    // increment amount is greater then the context.
    while ($count >= PLUMBER_INCREMENT);

    // Add the number of results for the current class to the total minus one
    // for the #total entry.
    $total += count($results[$class]) - 1;
  }
  worker_log('> ' . number_format($total) . ' relevant assertions found.');

  // Return the summaries as the results are not guaranteed to contain, and most
  // likely will not contain, all the assertions for all the tests.
  return $results;
}

/**
 * Clean results for a single class.
 *
 * @param $results
 *   Array of results.
 */
function plumber_results_clean(&$results) {
  foreach ($results as $key => &$result) {
    if ($key != '#total') {
      // Rename keys and make file path relative to job root.
      $result['type'] = $result['status'];
      $result['group'] = $result['message_group'];
      $result['file'] = str_replace(JOB_ROOT, '', $result['file']);

      // Remove unused and renamed keys.
      unset($result['message_id']);
      unset($result['test_id']);
      unset($result['test_class']);
      unset($result['status']);
      unset($result['message_group']);
    }
  }
}

/**
 * Obtain list of test meta data.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   An associative array keyed by test class and containing keys 'name' and
 *   'group' as specified in each tests getInfo().
 */
function plumber_meta(array $properties) {
  worker_log('Obtain test meta data.');

  // Cycle through all the lines in --list output and parse the test meta data
  // in the following format.
  // Group
  //  - Some test name (TestCaseClassName)
  //  - Another test (AnotherClass)
  worker_chdir();
  $lines = worker_execute_output(plumber_script() . ' --list');
  chdir(DRUPAL_ROOT);

  $group = 'Unknown';
  $info = array();
  foreach ($lines as $line) {
    // Check to see if line contains test information.
    if (!empty($line[1]) && $line[1] == '-') {
      // Attempt to parse line of test information and only bother to record it
      // if the meta data is for a relevant test class.
      if (preg_match('/^ - (?P<name>.*?) \((?P<class>\w+)\)$/m', $line, $match) && in_array($match['class'], $properties['classes'])) {
        $info[$match['class']] = array(
          'name' => $match['name'],
          'group' => $group,
        );
      }
    }
    else {
      // If line does not have test information then it can be assumed to have
      // group information even though first few lines of output will hit this
      // case since all test lines come after a valid group.
      $group = $line;
    }
  }
  worker_log('> Found meta data for ' . number_format(count($info)) . ' test class(es).');
  return $info;
}

/**
 * Generate the common part to the run-tests.sh command.
 *
 * @return
 *   Common part, or prefix, to the run-tests.sh command.
 */
function plumber_script() {
  if (!($php = worker_php_path())) {
    return FALSE;
  }

  // Escape arguments.
  $url = escapeshellarg(JOB_URL);
  $php = escapeshellarg($php);

  // Generate required (and general) portion of run-tests.sh command.
  return "php scripts/run-tests.sh --php $php --url $url";
}
