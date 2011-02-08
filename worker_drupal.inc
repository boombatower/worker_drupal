<?php
/**
 * @file
 * Common functions used by Drupal plugins.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Install Drupal on the primary database.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   TRUE if successful, otherwise FALSE or an error message.
 *
 * @see worker_perform() for a description of the properties array.
 */
function worker_drupal_install(array $properties) {
  worker_log('Prepare primary database.');
  if (!($connection = worker_database('drupal', $properties['database']))) {
    worker_log('Failed to prepare primary database.');
    return FALSE;
  }

  worker_log('Install Drupal ' . $properties['version'] . '.');

  // Copy the default.settings.php file as settings.php
  $default_settings = JOB_ROOT . '/sites/default/default.settings.php';
  $settings = JOB_ROOT . '/sites/default/settings.php';
  copy($default_settings, $settings);

  // Perform Drupal installation using drush.
  $installed = worker_drupal_install_drush($properties);

  // Ensure that both the default sites directory and settings.php are writable
  // so they can be removed later.
  chmod(JOB_ROOT . '/sites/default', 0755);
  chmod(JOB_ROOT . '/sites/default/settings.php', 0644);

  // If the installation was not a success then $installed will be FALSE or
  // contain the error message associated with the failure.
  if ($installed !== TRUE) {
    return $installed;
  }

  // Since Drupal installation has completed then go ahead and override/set the
  // specified list of variables.
  if (!worker_drupal_install_variables($properties['variables'])) {
    return 'Failed to set Drupal variable(s).';
  }

  return TRUE;
}

/**
 * Perform Drupal installation using drush.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   TRUE if successful, otherwise an error message or FALSE.
 */
function worker_drupal_install_drush(array $properties) {
  // Determine profile, database URL, and password.
  $db = worker_database_info('drupal', $properties['database']);
  $profile = $properties['version'] == 6 ? 'default' : 'minimal';
  $url = escapeshellarg("{$db['driver']}://{$db['username']}:{$db['password']}@{$db['host']}/{$db['database']}"); // @TODO Sqlite.
  $pass = escapeshellarg(worker_drupal_password());

  // Execute drush installation from within the job directory.
  worker_chdir();
  $success = worker_execute("drush si $profile --yes --db-url=$url --site-name='Worker generated installation' --account-name=admin --account-pass=$pass");
  chdir(DRUPAL_ROOT);
  return $success ? TRUE : 'Drupal installation failed';
}

/**
 * Set a list of Drupal variables.
 *
 * @param $variables
 *   List of Drupal variables.
 * @return
 *   TRUE if successful, otherwise FALSE.
 */
function worker_drupal_install_variables(array $variables) {
  worker_log('Set Drupal variable(s).');
  $success = TRUE;
  foreach ($variables as $name => $value) {
    // If the variable was not set successfully then log the event and set
    // success to false, but continue so that if the variable set had an error
    // for some reason other then database connectivity that may be noticed.
    if (!worker_drupal_variable_set($name, $value)) {
      worker_log('> Failed to set variable ' . $name . '.');
      $success = FALSE;
    }
  }
  return $success;
}

/**
 * Set a Drupal variable.
 *
 * @param $name
 *   Name of the variable.
 * @param $value
 *   Value of the variable.
 * @return
 *   TRUE if successful, otherwise FALSE.
 */
function worker_drupal_variable_set($name, $value) {
  // Escape name and value arguments.
  $name = escapeshellarg($name);
  $value = escapeshellarg($value);

  // Invoke drush vset from within the job directory.
  worker_chdir();
  $success = worker_execute("drush vset --always-set $name $value");
  chdir(DRUPAL_ROOT);
  return $success;
}

/**
 * Get the admin password for the Drupal installation.
 *
 * @return
 *   The admin password for the Drupal installation.
 */
function worker_drupal_password() {
  return drupal_static(__FUNCTION__, mt_rand(1000, 1000000));
}

/**
 * Enable the module or modules on the job site.
 *
 * @param $modules
 *   A single module or list of modules.
 * @param $version
 *   Drupal core version.
 * @return
 *  TRUE if successful, otherwise FALSE.
 */
function worker_drupal_module_enable($modules, $version) {
  // If a single module was specified then create an array.
  if (!is_array($modules)) {
    $modules = array($modules);
  }

  worker_chdir();
  $success = worker_execute('drush --yes en ' . implode(' ', $modules));
  chdir(DRUPAL_ROOT);
  return $success;
}

/**
 * Get the path to a module relative to the JOB_ROOT.
 *
 * @param $module
 *   Name of the module.
 * @return
 *   Relative path to the module, otherwise FALSE.
 */
function worker_drupal_module_path($module) {
  $modules = &drupal_static(__FUNCTION__);

  // If the module paths have not been loaded the scan the job directory for
  // all modules and store the directories in which they are found.
  if (!$modules) {
    worker_log('Scan for modules.');
    foreach (array('modules', 'sites/all', 'sites/default') as $directory) {
      $files = file_scan_directory(JOB_ROOT . '/' . $directory, '/\.module$/m');
      foreach ($files as $file) {
        $modules[$file->name] = str_replace(array(JOB_ROOT . '/', '/' . $file->filename), '', $file->uri);
      }
    }
    worker_log('> Found ' . count($modules) . '.');
  }

  return !empty($modules[$module]) ? $modules[$module] : FALSE;
}
