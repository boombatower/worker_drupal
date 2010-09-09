<?php
/**
 * @file
 * Provide Drupal scan worker plugin.
 *
 * - php syntax
 * - install
 * - list tests
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

$plugin = array(
  'title' => t('Drupal scan'),
  'description' => t('Provide Drupal scan worker plugin.'),
  'perform' => 'dscan_perform',
);

/**
 * Perform the job.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   Result of the command run.
 */
function dscan_perform(array $properties) {
  if ($errors = dscan_syntax($properties)) {
    return $errors;
  }
  elseif ($errors === FALSE) {
    return FALSE;
  }

  if (!conduit_drupal_install_perform($properties)) {
    return FALSE;
  }

  return TRUE;
}

/**
 * Check for PHP syntax errors.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   Associative array of syntax errors keyed by path and containing 'messaage'
 *   and 'context' (an array of php lines), or FALSE if an error occured.
 */
function dscan_syntax(array $properties) {
  worker_log('Assemble list of PHP files to syntax check.');
  if (!($files = worker_file_list($properties))) {
    return FALSE;
  }
  worker_log('Syntax check ' . count($files) . ' PHP file(s).');

  worker_chdir();
  $errors = array();
  foreach ($files as $file) {
    worker_log('> Check ' . $file . '.');
    if ($output = worker_execute_output('php -l -f ' . escapeshellarg($file))) {
      $output = $output[0];
      if (strpos($output, 'No syntax errors detected') === FALSE) {
        $errors[$file] = array(
          'message' => $output,
          'context' => array(),
        );

        if (preg_match('/line (?P<line>\d+)$/m', $output, $match)) {
          $lines = file(JOB_ROOT . '/' . $file);
          $errors[$file]['context'] = array_slice($lines, $match['line'] - 4, 7, TRUE);
        }
      }
    }
    else {
      worker_log('PHP syntax check did not respond properly.');
      return FALSE;
    }
  }
  chdir(DRUPAL_ROOT);

  if (!empty($errors)) {
    worker_log_list('PHP syntax errors found in the following file(s):', array_keys($errors));
  }
  return $errors;
}
