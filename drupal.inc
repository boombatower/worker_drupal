<?php
/**
 * @file
 * Common Drupal conduit functions.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Install Drupal on the primary database.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @return
 *   TRUE if successful, otherwise FALSE.
 */
function worker_drupal_install(array $properties) {
  worker_log('Prepare primary database.');
  if (!worker_database('drupal', $properties['database'])) {
    worker_log('Failed to prepare primary database.');
    return FALSE;
  }

  worker_log('Install Drupal ' . $properties['version'] . '.');

  // Copy the default.settings.php file as settings.php
  $default_settings = JOB_ROOT . '/sites/default/default.settings.php';
  $settings = JOB_ROOT . '/sites/default/settings.php';
  copy($default_settings, $settings);

  // Invoke either the Drupal 6 installation function ro the Drupal 7 and above
  // installation function and wait until after chmod() before checking result.
  $url = url('', array('absolute' => TRUE)) . '/job';
  $function = 'worker_drupal_install_' . min($properties['version'], 7);
  $installed = $function($properties, $url);

  // Ensure that both the default sites directory and settings.php are writable
  // so they can be removed later.
  chmod(JOB_ROOT . '/sites/default', 0755);
  chmod(JOB_ROOT . '/sites/default/settings.php', 0644);

  if (!$installed) {
    worker_log('Drupal installation failed.');
    return FALSE;
  }

  return TRUE;
}

/**
 * Perform Drupal 7 installation using non-interactive script.
 *
 * For Drupal 7 and above we can use the non-interactive installation method
 * which requires a file to be generated with the site configuration
 * information and placed in the root of the Drupal install.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @param $url
 *   Absolute ULR to job site.
 * @return
 *   TRUE if successful, otherwise FALSE.
 */
function worker_drupal_install_7(array $properties, $url) {
  // Generate array of settings information required for installation and dump
  // array in installation script template to a file in the root of job site.
  $settings = array(
    'parameters' => array(
      'profile' => 'minimal',
      'locale' => 'en',
    ),
    'forms' => array(
      'install_settings_form' => worker_database_info('drupal', $properties['database']),
      'install_configure_form' => array(
        'site_name' => 'Conduit generated installation',
        'site_mail' => 'conduit@example.com',
        'account' => array(
          'name' => 'admin',
          'mail' => 'admin@example.com',
          'pass' => array(
            'pass1' => worker_drupal_password(),
            'pass2' => worker_drupal_password(),
          ),
        ),
        'update_status_module' => array(
          1 => TRUE,
          2 => TRUE,
        ),
        'clean_url' => TRUE,
      ),
    ),
  );
  $out = <<<'EOT'
<?php
define('MAINTENANCE_MODE', 'install');
define('DRUPAL_ROOT', getcwd());
require_once DRUPAL_ROOT . '/includes/install.core.inc';
$settings = [settings];
install_drupal($settings);

EOT;
  $out = str_replace('[settings]', var_export($settings, TRUE), $out);
  file_put_contents(JOB_ROOT . '/install.conduit.php', $out);

  // To compelete the installation the generated file needs to be invoked
  // using the URL through which the site will be accessed.
  $request = drupal_http_request($url . '/install.conduit.php', array('timeout' => 60));
  if ($request->code != 200) {
    return FALSE;
  }
  return TRUE;
}

/**
 * Perform Drupal 6 installation using Browser class.
 *
 * @param $properties
 *   Associative array of properties defining the job.
 * @param $url
 *   Absolute ULR to job site.
 * @return
 *   TRUE if successful, otherwise FALSE.
 */
function worker_drupal_install_6(array $properties, $url) {
  $browser = new Browser();

  // Step: Select an installation profile.
  // Step: Choose language.
  $result = $browser->get("$url/install.php?profile=default&locale=en");
  if ($result['code'] != 200) {
    worker_log('> Failed to start installation process.');
    return FALSE;
  }

  // Step: Database configuration.
  $database = worker_database_info('drupal', $properties['database']);
  $post = array();
  if ($browser->getPage()->xpath('//input[@name="driver"]')) {
    $post['driver'] = $properties['database'];
  }
  $post['db_path'] = $database['database'];
  $post['db_user'] = $database['username'];
  $post['db_pass'] = $database['password'];
  $post['db_host'] = $database['host'];
  $result = $browser->post(NULL, $post, 'Save and continue');
  if ($result['code'] != 302) {
    worker_log('> Failed to submit database information.');
    return FALSE;
  }

  // Step: Site configuration.
  $post = array();
  $post['site_name'] = 'Conduit generated installation';
  $post['site_mail'] = 'conduit@example.com';
  $post['account[name]'] = 'admin';
  $post['account[mail]'] = 'admin@example.com';
  $post['account[pass][pass1]'] = $post['account[pass][pass2]'] = worker_drupal_password();
  $post['update_status_module[1]'] = FALSE;
  $result = $browser->post(NULL, $post, 'Save and continue');
  if ($result['code'] != 200 || !$browser->getPage()->xpath('//h2[text()="Drupal installation complete"]')) {
    worker_log('> Failed to complete site configuration.');
    return FALSE;
  }
  return TRUE;
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
