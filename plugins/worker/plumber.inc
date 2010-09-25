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
 * property must be greater than PLUMBER_INCRAMENT.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * The maximum number of assertion to process at one time.
 */
define('PLUMBER_INCRAMENT', 1000);

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
 *   Associative array of properties defining the job.
 * @return
 *   Associative array containing the 'type' of result being returned and the
 *   associated value that goes along with it, either 'errors' or 'classes'.
 */
function plumber_perform(array $properties) {
  // Load common Drupal conduit functions.
  require drupal_get_path('module', 'conduit_drupal') . '/drupal.inc';

  if (($error = worker_drupal_install($properties)) !== TRUE) {
    return array(
      'type' => 'install',
      'error' => $error,
    );
  }

  worker_log('Enable simpletest.');
  if (!worker_drupal_module_enable('simpletest', $properties['version'])) {
    return array(
      'type' => 'enable',
      'error' => 'Failed to enable simpletest.',
    );
  }

  if (!($log = plumber_plumb($properties))) {
    // Unrecoverable error related to configuration occurred.
    return FALSE;
  }

  $results = plumber_results($properties);

  return TRUE;
}

/**
 * Plumb the plumbing (ie. run tests).
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   Log of the length of time each testcase took to complete, otherwise FALSE.
 * @see worker_execute_concurrent()
 */
function plumber_plumb(array $properties) {
  // Determine the location of the PHP executable from whereis command output.
  if (!(($php = current(worker_execute_output('whereis php'))) && ($php = explode(' ', $php)) && !empty($php[1]))) {
    worker_log('Could not determine the location of the PHP executable.');
    return FALSE;
  }

  // Escape arguments.
  $url = escapeshellarg(JOB_URL);
  $php = escapeshellarg($php[1]);

  // Generate list of commands to run all test cases.
  $commands = array();
  $command = "php scripts/run-tests.sh --php $php --url $url --class ";
  foreach ($properties['classes'] as $class) {
    $commands[$class] = $command . $class;
  }

  // Run tests using manual concurrency.
  worker_log('Run ' . count($properties['classes']) . ' test case(s).');
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
 *   An associative containing keys 'summaries' and 'results'. The summaries
 *   key contains an associative array keyed by the test class and containing
 *   the count of assertions by type. The results key contains a list of
 *   relevant assertions, assertions that either were of the specified status
 *   or were part of the context around the latter type of assertion.
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

  // Build an associative array of summaries keyed by the test class and
  // containing the count of each status type found in the results for the test
  // class. Also compile a list of classes that have status types that are
  // found in the 'statues' property.
  $summaries = array();
  $classes = array();
  while ($summary = $result->fetchAssoc()) {
    if (empty($summaries[$summary['test_class']])) {
      $summaries[$summary['test_class']] = array();
    }
    $summaries[$summary['test_class']][$summary['status']] = $summary['count'];

    if (in_array($summary['status'], $properties['statuses'])) {
      $classes[$summary['test_class']] = $summary['test_class'];
    }
  }
  worker_log('> ' . number_format(count($classes)) . ' relevant class(es) found.');

  // Calculate the context length ahead of time since it is static. The length
  // includes the amount of context on either side of the relevant assertion.
  $context_length = $properties['context'] * 2 + 1;

  // Cycle through the classes that are know to have assertions with statuses
  // that are to be included in the results.
  $results = array();
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
        ->range(0, PLUMBER_INCRAMENT)
        ->execute()
        ->fetchAllAssoc('message_id', PDO::FETCH_ASSOC);

      // Cycle through each assertion looking for assertions that should be
      // reported and grab the relevant context around the assertion.
      $i = 0;
      foreach ($assertions as $assertion) {
        if (in_array($assertion['status'], $properties['statuses'])) {
          // Since the $assertions array is keyed by message_id adding an
          // assertion multiple time will not result in duplicate entries. In
          // case the assertion is at the beginning of the list we need to
          // ensure the offset parameter to array_splice() is non-negative.
          $length = $properties['context'] > $i ? $context_length - ($properties['context'] - $i) : $context_length;
          $results += array_slice($assertions, max($i - $properties['context'], 0), $length, TRUE);

          // If the number of relevant assertions has gone over the maximum
          // number allowed then break out of the processing loops.
          if (count($results) >= PLUMBER_MAX) {
            break 3;
          }
        }
        $i++;
      }

      // Store the number of assertions processed in the loop since the value
      // may be used multiple times.
      $count = count($assertions);
    }
    // If the number of assertions returned is the same as or greater then, due
    // to context from previous loop interation, the incrament amount then
    // there is a possibility for more assertions to exist. This assumes the
    // incrament amount is greater then the context.
    while ($count >= PLUMBER_INCRAMENT);
  }
  worker_log('> ' . number_format(count($results)) . ' relevant assertions found.');

  // Since the results are not guaranteed, and most likely will not, to contain
  // all the assertions for all the tests we need to return the summaries.
  return array(
    'summaries' => $summaries,
    'results' => array_values($results),
  );
}
