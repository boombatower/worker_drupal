<?php
/**
 * @file
 * Provide Drupal coder integration.
 *
 * Requires access to drush and the following patches:
 *  - http://drupal.org/node/946978
 *  - http://drupal.org/node/948124
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Number of files to be reviewed at the same time.
 */
define('CODER_CHUNK', 30);

$plugin = array(
  'title' => t('Drupal coder'),
  'description' => t('Provide Drupal coder integration.'),
  'perform' => 'coder_perform',
);

/**
 * Perform the job.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   Multi-dimentional, associative array of coder_review results keyed by the
 *   file path and containing an array of messages with the keys: line,
 *   severity, and message. The line key will FALSE if there is no line
 *   associated with the message.
 *
 * @see worker_perform() for a description of other keys in this array.
 */
function coder_perform(array $properties) {
  // Assemble list of relevant files and append the path reletive to
  // DRUPAL_ROOT to the JOB_ROOT since coder will be executed from the host.
  worker_log('Assemble list of relevant files.');
  $files = worker_file_list($properties);
  $path = str_replace(DRUPAL_ROOT . '/', '', JOB_ROOT);
  array_walk($files, 'coder_file_path', $path);

  // If the severity to be recorded is 'normal' then pass nothing to drush.
  $severity = $properties['severity'] == 'normal' ? '' : $properties['severity'] . ' ';

  // Build the common part of the drush coder_review command and determine the
  // file in which the results will be logged.
  $command = 'drush coder-review ' . $severity . implode(' ', $properties['reviews']) . ' ';
  $log_file = JOB_ROOT . '/coder_review.log';

  // Split the files into chunks that will be processed separately and build
  // the complete drush command.
  $chunks = array_chunk($files, CODER_CHUNK);
  $commands = array();
  foreach ($chunks as $chunk) {
    $commands[] = $command . implode(' ', $chunk) . ' >> ' . $log_file;
  }

  // Execute the set of drush coder_review commands.
  $log = worker_execute_concurrent($commands, 100);

  // Parse the resulting log file and collect the results.
  $results = array();
  if (file_exists($log_file) && ($lines = file($log_file))) {
    $length = strlen($path);
    $file = FALSE;
    foreach ($lines as $line) {
      // Relevant lines either match the format
      // '+line_number: [severity] message' where the line_number is optional or
      // provide a file reference that starts with the $path determined above.
      if (preg_match('/(\+(?P<line>-?\d+): )?\[(?P<severity>\w+)\] (?P<message>.*?)$/m', $line, $match)) {
        $results[$file][] = array(
          'line' => empty($match['line']) ? FALSE : $match['line'],
          'severity' => $match['severity'],
          'message' => $match['message'],
        );
      }
      elseif (substr($line, 0, $length) == $path) {
        $file = substr($line, $length + 1, -2);
        $results[$file] = array();
      }
    }
  }

  if ($properties['annotate']) {
    // @TODO Implement annotate functionality (use concurrency).
  }

  return $results;
}

/**
 * Append the path to the file.
 *
 * @see array_walk()
 */
function coder_file_path(&$file, $key, $path) {
  $file = $path . '/' . $file;
}
