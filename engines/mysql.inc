<?php

/**
 * @file
 * Schema module enhancements to DatabaseSchema_mysql
 */

class SchemaDatabaseSchema_mysql extends DatabaseSchema_mysql {

  /**
   * @var string
   */
  protected $version;

  /**
   * @var string
   */
  protected $versionComment;

  /**
   * {@inheritdoc}
   */
  public function __construct($connection) {
    parent::__construct($connection);
    // Read the version data to know how to interpret information_schema.
    $this->readVersionData();
  }

  /**
   * Retrieve generated SQL to create a table from a Backdrop schema definition.
   *
   * @param string $name
   *   The name of the table to create.
   * @param array $table
   *   A Schema API table definition array.
   * @return array
   *   An array of SQL statements to create the table.
   */
  public function getCreateTableSql($name, array $table) {
    return parent::createTableSql($name, $table);
  }

  public function schema_type_map() {
    static $map;
    if (!isset($map)) {
      $map = array_flip(array_map('strtolower', $this->getFieldTypeMap()));
    }
    return $map;
  }

  /**
   * Overrides DatabaseSchema_mysql::getPrefixInfo().
   *
   * @todo Remove when https://drupal.org/node/2223073 is fixed in core.
   */
  protected function getPrefixInfo($table = 'default', $add_prefix = TRUE) {
    $info = array('prefix' => $this->connection->tablePrefix($table));
    if ($add_prefix) {
      $table = $info['prefix'] . $table;
    }
    if (($pos = strpos($table, '.')) !== FALSE) {
      $info['database'] = substr($table, 0, $pos);
      $info['table'] = substr($table, ++$pos);
    }
    else {
      $db_info = $this->connection->getConnectionOptions();
      $info['database'] = $db_info['database'];
      $info['table'] = $table;
    }
    return $info;
  }

  /**
   * Overrides DatabaseSchema_mysql::getFieldTypeMap().
   */
  public function getFieldTypeMap() {
    $map = &backdrop_static('SchemaDatabaseSchema_mysql::getFieldTypeMap');
    if (!isset($map)) {
      $map = parent::getFieldTypeMap();
      backdrop_alter('schema_field_type_map', $map, $this, $this->connection);
    }
    return $map;
  }

  /**
   * Retrieves the schema for the database's actual structure.
   *
   * @param string|null $connection
   *  (optional) The name of a database connection. If omitted, 'default' is
   *  used.
   * @param string|null $table_name
   *  (optional) The name of a table to limit the returned data to. If omitted,
   *  data on all tables is returned.
   *
   * @return array
   *  An array of data about the database, in the same format as used by the
   *  Schema API.
   */
  public function inspect($connection = NULL, $table_name = NULL) {
    // Support the deprecated connection parameter.
    if (isset($connection) && $connection != $this->connection->getKey()) {
      $this->connection = Database::getConnection('default', $connection);
    }

    // Get the current database name
    $info = $this->connection->getConnectionOptions();
    $database = $info['database'];

    // Apply table prefixes.
    if (isset($table_name)) {
      $table_info = $this->getPrefixInfo($table_name);
      if (!empty($table_info['database']) && $database != $table_info['database']) {
        $database = $table_info['database'];
      }
      $table_name = $table_info['table'];
    }

    $tables = array();
    $args = array(':database' => $database);
    $sql = 'SELECT table_name, table_comment
            FROM information_schema.tables
            WHERE table_schema=:database ';
    if (isset($table_name)) {
      $sql .= 'AND table_name = :table ';
      $args[':table'] = $table_name;
    }
    $res = $this->connection->query($sql, $args);
    foreach ($res as $r) {
      $tables[$r->table_name]['description'] = $r->table_comment;
    }

    $sql = 'SELECT table_name, column_type, column_name, column_default,
                   extra, is_nullable, numeric_scale, column_comment, data_type
            FROM information_schema.columns
            WHERE table_schema=:database ';
    if (isset($table_name)) {
      $sql .= 'AND table_name = :table ';
    }
    $sql .= 'ORDER BY table_name, ordinal_position';

    $res = $this->connection->query($sql, $args);
    foreach ($res as $r) {
      $r->new_table_name = schema_unprefix_table($r->table_name, $this->connection);

      $numeric = $this->isColumnNumeric($r);
      $col = array();
      $col['type'] = $r->column_type;
      if (preg_match('@([a-z]+)(?:\((\d+)(?:,(\d+))?\))?\s*(unsigned)?@', $col['type'], $matches)) {
        list($col['type'], $col['size']) = schema_schema_type($matches[1], $r->table_name, $r->column_name, 'mysql');
        if (isset($matches[2])) {
          if ($col['type'] == 'numeric' || $col['type'] == 'float' || $col['type'] == 'double') {
            $col['precision'] = $matches[2];
            $col['scale'] = $matches[3];
          }
          elseif (!$numeric) {
            $col['length'] = $matches[2];
          }
        }
        if (isset($matches[4])) {
          $col['unsigned'] = TRUE;
        }
      }
      if ($col['type'] == 'int' && isset($r->extra) &&
          $r->extra == 'auto_increment') {
        $col['type'] = 'serial';
      }
      $col['not null'] = ($r->is_nullable == 'YES' ? FALSE : TRUE);
      $column_default = $this->getColumnDefault($r);
      if (! is_null($column_default)) {
        $col['default'] = $column_default;
      }
      $col['description'] = $r->column_comment;
      $tables[$r->table_name]['fields'][$r->column_name] = $col;
      // At this point, $tables is indexed by the raw db table name - save the unprefixed
      // name for later use
      $tables[$r->table_name]['name'] = $r->new_table_name;
    }

    $sql = 'SELECT table_name, column_name, index_name, sub_part, non_unique
            FROM information_schema.statistics
            WHERE table_schema=:database ';
    if (isset($table_name)) {
      $sql .= 'AND table_name = :table ';
    }
    $sql .= 'ORDER BY table_name, index_name, seq_in_index';

    $res = $this->connection->query($sql, $args);
    foreach ($res as $r) {
      if (isset($r->sub_part) && !is_null($r->sub_part)) {
        $col = array($r->column_name, intval($r->sub_part));
      }
      else {
        $col = $r->column_name;
      }
      if ($r->index_name == 'PRIMARY') {
        $type = 'primary key';
        $tables[$r->table_name][$type][] = $col;
        continue;
      }
      elseif ($r->non_unique == 0) {
        $type = 'unique keys';
      }
      else {
        $type = 'indexes';
      }
      $tables[$r->table_name][$type][$r->index_name][] = $col;
    }

    // Add information about foreign keys by querying tables in the
    // information_schema. This adds a 'foreign keys' key to the array for each
    // table.
    $sql = 'SELECT *
            FROM information_schema.KEY_COLUMN_USAGE
            WHERE table_schema = :database';
    if (isset($table_name)) {
      $sql .= ' AND table_name = :table';
    }
    $res = $this->connection->query($sql, $args);
    foreach ($res as $r) {
      if ($r->REFERENCED_TABLE_NAME) {
        if (empty($tables[$r->TABLE_NAME]['foreign keys'][$r->CONSTRAINT_NAME])) {
          $tables[$r->TABLE_NAME]['foreign keys'][$r->CONSTRAINT_NAME] = array(
            'table' => $r->REFERENCED_TABLE_NAME,
            'columns' => array(),
          );
        }
        $tables[$r->TABLE_NAME]['foreign keys'][$r->CONSTRAINT_NAME]['columns'][$r->COLUMN_NAME] = $r->REFERENCED_COLUMN_NAME;
      }
    }

    // Now, for tables which we have unprefixed, index $tables by the unprefixed name
    foreach ($tables as $tablename => $table) {
      $newname = $tables[$tablename]['name'];
      if ($tablename != $newname) {
        $tables[$newname] = $table;
        unset($tables[$tablename]);
      }
    }

    return $tables;
  }

  /**
   * Returns whether the column record is for a numeric field.
   *
   * @param $record
   *   The information_schema database record object.
   *
   * @return bool
   *   TRUE if the record is numeric; FALSE otherwise.
   */
  protected function isColumnNumeric($record) {
    return !is_null($record->numeric_scale);
  }

  /**
   * Returns the column default from the information_schema record.
   *
   * @param $record
   *   The information_schema database record object.
   *
   * @return mixed
   *   The column's default value.
   */
  protected function getColumnDefault($record) {
    // Is it safer to read the version suffix or use a partial match?
    if (stripos($this->version, '-MariaDB') !== FALSE) {
      $default = $this->getMariaDBColumnDefault($record);
    }
    else {
      $default = $record->column_default;
    }
    if (!is_null($default) && $this->isColumnNumeric($record)) {
      // XXX floats!
      $default = intval($default);
    }
    return $default;
  }

  /**
   * Return the column default from a MariaDB information_schema record.
   *
   * From MariaDB 10.2.7, literals are quoted to distinguish them from
   * expressions. NULL means that the column has no default. In MariaDB 10.2.6
   * and earlier, no quotes were used for any type of default and NULL can
   * either mean that there is no default, or that the default column value is
   * NULL.
   *
   * @param $record
   *   The information_schema database record object.
   *
   * @return mixed
   *   The column's default value.
   *
   * @see https://mariadb.com/kb/en/library/information-schema-columns-table/
   * @see https://jira.mariadb.org/browse/MDEV-13132
   */
  protected function getMariaDBColumnDefault($record) {
    $default = $record->column_default;

    // v10.2.7+: The string 'NULL' indicates an explicit NULL default value;
    // an actual NULL value indicates no default value.
    // <v10.2.7: An actual NULL value is used to indicate both an explicit NULL
    // default value and not having a default value set.
    if ($default == 'NULL' || is_null($default)) {
      return NULL;
    }

    if (version_compare($this->version, '10.2.7', '<')) {
      return $default;
    }

    // Not all types are quoted, this list was made by looking at the changes to
    // default values in the following commit and might not be complete:
    // https://github.com/MariaDB/server/commit/291411c96ceb914950731df6c36b32fc83b58acf
    $affected_types = [
      'binary',
      'char',
      'longtext',
      'varchar',
      'date',
      'datetime',
      'time',
      'timestamp',
      'enum',
      'set',
    ];
    if (!in_array($record->data_type, $affected_types)) {
      return $default;
    }

    // Strip the quotes if present and show a warning if not.
    $stripped = $this->stripSingleQuotes($default);
    if ($stripped == $default) {
      // Maybe it's an expression?
      backdrop_set_message(t("Schema can't handle one or more of the default values"), 'warning', FALSE);
    }

    return $stripped;
  }

  /**
   * Read the version data from the database and cache it.
   */
  protected function readVersionData() {
    $sql = "SHOW VARIABLES WHERE Variable_name IN ('version', 'version_comment')";
    $res = $this->connection->query($sql)->fetchAllKeyed(0);
    $this->version = $res['version'];
    $this->versionComment = $res['version_comment'];
  }

  /**
   * Attempt to strip single quotes from a string.
   *
   * @param string $string
   *   The string to be stripped.
   *
   * @return string
   *   The stripped string if it is wrapped in single quotes; the original
   *   string otherwise.
   */
  protected function stripSingleQuotes($string) {
    if (backdrop_substr($string, 0, 1) == "'" && backdrop_substr($string, -1) == "'") {
      return backdrop_substr($string, 1, -1);
    }
    return $string;
  }

}
