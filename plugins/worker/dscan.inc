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
    return $errors;
  }
  elseif ($errors === FALSE) {
    return FALSE;
  }

  // Load common Drupal conduit functions.
  require drupal_get_path('module', 'conduit_drupal') . '/drupal.inc';

  if (!worker_drupal_install($properties)) {
    return FALSE;
  }

  return TRUE;
}
