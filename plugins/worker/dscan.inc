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
  if ($errors = worker_syntax($properties)) {
    // Encapsulate the errors so the tye can be easily determined.
    return array(
      'type' => 'syntax',
      'errors' => $errors,
    );
  }
  elseif ($errors === FALSE) {
    // An error occured and should be treated a job failure.
    return FALSE;
  }

  // Load common Drupal conduit functions.
  require drupal_get_path('module', 'conduit_drupal') . '/drupal.inc';

  if (($error = worker_drupal_install($properties)) !== TRUE) {
    // Encapsulate the error so the tye can be easily determined.
    return array(
      'type' => 'install',
      'error' => $error,
    );
  }

  return TRUE;
}
