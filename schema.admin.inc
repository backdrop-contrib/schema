<?php

/**
 * @file
 * Administrative page callbacks for the Schema module.
 */

function schema_settings_form($form, &$form_state) {
  $form['#config'] = 'schema.settings';
  $connection_options = schema_get_connection_options();
  $form['schema_database_connection'] = array(
    '#type' => 'select',
    '#title' => t('Database connection to use'),
    '#default_value' => config_get('schema.settings', 'schema_database_connection'),
    '#options' => $connection_options,
    '#description' => t('If you use a secondary database other than the default Backdrop database you can select it here and use schema\'s "compare" and "inspect" functions on that other database.'),
    '#access' => count($connection_options) > 1,
  );
  $form['schema_status_report'] = array(
    '#type' => 'checkbox',
    '#title' => t('Include schema comparison reports in site status report'),
    '#default_value' => config_get('schema.settings', 'schema_status_report'),
    '#description' => t('When checked, schema comparison reports are run on the Administer page, and included in the site status report.'),
  );
  $form['schema_suppress_type_warnings'] = array(
    '#type' => 'checkbox',
    '#title' => t('Suppress schema warnings.'),
    '#default_value' => config_get('schema.settings', 'schema_suppress_type_warnings'),
    '#description' => t('When checked, missing schema type warnings will be suppressed.'),
  );

  return system_settings_form($form);
}
