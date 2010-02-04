/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/****************************************************************
 * Filename: CliMgmtUtils.cc
 * Purpose: This file contains various utility functions which
 *          call the INKMgmtAPI.
 *
 * 
 ****************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include "CliMgmtUtils.h"
#include "CliDisplay.h"
#include "ink_resource.h"
#include "definitions.h"
#include "ConfigCmd.h"
#include <inktomi++.h>

void Cli_DisplayMgmtAPI_Error(INKError status);

// Get a records.config variable by name
INKError
Cli_RecordGet(const char *rec_name, INKRecordEle * rec_val)
{
  INKError status;
  if ((status = INKRecordGet((char *) rec_name, rec_val))) {
    Cli_Debug(ERR_RECORD_GET, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Get an integer type records.config variable
INKError
Cli_RecordGetInt(char *rec_name, INKInt * int_val)
{
  INKError status;
  if ((status = INKRecordGetInt(rec_name, int_val))) {
    Cli_Debug(ERR_RECORD_GET_INT, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Get an counter type records.config variable
INKError
Cli_RecordGetCounter(char *rec_name, INKCounter * ctr_val)
{
  INKError status;
  if ((status = INKRecordGetCounter(rec_name, ctr_val))) {
    Cli_Debug(ERR_RECORD_GET_COUNTER, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Get a float type records.config variable
INKError
Cli_RecordGetFloat(char *rec_name, INKFloat * float_val)
{
  INKError status;
  if ((status = INKRecordGetFloat(rec_name, float_val))) {
    Cli_Debug(ERR_RECORD_GET_FLOAT, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Get a string type records.config variable
INKError
Cli_RecordGetString(char *rec_name, INKString * string_val)
{
  INKError status;
  if ((status = INKRecordGetString(rec_name, string_val))) {
    Cli_Debug(ERR_RECORD_GET_STRING, rec_name);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Use a string to set a records.config variable
INKError
Cli_RecordSet(const char *rec_name, const char *rec_value, INKActionNeedT * action_need)
{
  INKError status;
  if ((status = INKRecordSet((char *) rec_name, (INKString) rec_value, action_need))) {
    Cli_Debug(ERR_RECORD_SET, rec_name, rec_value);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Set an integer type records.config variable
INKError
Cli_RecordSetInt(char *rec_name, INKInt int_val, INKActionNeedT * action_need)
{
  INKError status;
  if ((status = INKRecordSetInt(rec_name, int_val, action_need))) {
    Cli_Debug(ERR_RECORD_SET_INT, rec_name, int_val);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

// Set a float type records.config variable
INKError
Cli_RecordSetFloat(char *rec_name, INKFloat float_val, INKActionNeedT * action_need)
{
  INKError status;
  if ((status = INKRecordSetFloat(rec_name, float_val, action_need))) {
    Cli_Debug(ERR_RECORD_SET_FLOAT, rec_name, float_val);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}


// Set a string type records.config variable
INKError
Cli_RecordSetString(char *rec_name, INKString str_val, INKActionNeedT * action_need)
{
  INKError status;
  if ((status = INKRecordSetString(rec_name, str_val, action_need))) {
    Cli_Debug(ERR_RECORD_SET_STRING, rec_name, str_val);
    Cli_DisplayMgmtAPI_Error(status);
  }
  return status;
}

void
Cli_DisplayMgmtAPI_Error(INKError status)
{
  switch (status) {
  case INK_ERR_OKAY:           // do nothing
    break;
  case INK_ERR_READ_FILE:
    Cli_Printf("\nERROR: Unable to read config file.\n\n");
    break;
  case INK_ERR_WRITE_FILE:
    Cli_Printf("\nERROR: Unable to write config file.\n\n");
    break;
  case INK_ERR_PARSE_CONFIG_RULE:
    Cli_Printf("\nERROR: Unable to parse config file.\n\n");
    break;
  case INK_ERR_INVALID_CONFIG_RULE:
    Cli_Printf("\nERROR: Invalid Configuration Rule in config file.\n\n");
    break;
  case INK_ERR_NET_ESTABLISH:
    Cli_Printf("\nERROR: Unable to establish connection to traffic_manager.\n"
               "       Ability to make configuration changes depends on traffic_manager.\n");
    break;
  case INK_ERR_NET_READ:
    Cli_Printf("\nERROR: Unable to read data from traffic_manager.\n"
               "       Ability to monitor the system changes depends on traffic_manager.\n");
    break;
  case INK_ERR_NET_WRITE:
    Cli_Printf("\nERROR: Unable to write configuration data to traffic_manager.\n"
               "       Ability to make configuration changes depends on traffic_manager.\n");
    break;
  case INK_ERR_NET_EOF:
    Cli_Printf("\nERROR: Unexpected EOF while communicating with traffic_manager.\n"
               "       Ability to make configuration changes depends on traffic_manager.\n");
    break;
  case INK_ERR_NET_TIMEOUT:
    Cli_Printf("\nERROR: Timed-out while communicating with traffic_manager.\n"
               "       Ability to make configuration changes depends on traffic_manager.\n");
    break;
  case INK_ERR_SYS_CALL:
    Cli_Printf("\nERROR: Internal System Call failed.\n\n");
    break;
  case INK_ERR_PARAMS:
    Cli_Printf("\nERROR: Invalid parameters passed to a function.\n\n");
    break;
  case INK_ERR_FAIL:
    Cli_Printf("\nERROR: Invalid parameter specified.\n" "       Check parameters for correct syntax and type.\n\n");
    break;
  default:
    Cli_Printf("\nERROR: Undocumented Error. Status = %d.\n\n", status);
    break;
  }
}

// Retrieve and display contents of a rules file
INKError
Cli_DisplayRules(INKFileNameT fname)
{
  INKError status;
  char *text;
  int size = 0, version = 0;

  if ((status = INKConfigFileRead(fname, &text, &size, &version))) {
    Cli_Debug(ERR_CONFIG_FILE_READ, fname);
    Cli_DisplayMgmtAPI_Error(status);
  } else {
    if (size) {
      // Fix INKqa12220: use printf directly since Cli_Printf may
      // not allocate enough buffer space to display the file contents
      puts(text);
      xfree(text);
    } else {
      Cli_Printf("no rules\n");
    }
  }

  return status;
}

// Retrieve and use config file from remote URL
INKError
Cli_SetConfigFileFromUrl(INKFileNameT file, const char *url)
{
  char *buf;
  int size = 0;
  int version = -1;
  INKError status;

  Cli_Debug("Cli_SetConfigFileFromUrl: file %d url %s\n", file, url);

  // read config file from Url
  if ((status = INKReadFromUrl((char *) url, NULL, NULL, &buf, &size))) {
    Cli_Debug(ERR_READ_FROM_URL, url);
    Cli_DisplayMgmtAPI_Error(status);
    return status;
  }

  Cli_Debug("Cli_SetConfigFileFromUrl: size %d version %d\n", size, version);

  Cli_Debug("Cli_SetConfigFileFromUrl: buf\n%s\n", buf);

  // write config file
  if ((status = INKConfigFileWrite(file, buf, size, version))) {
    Cli_Debug(ERR_CONFIG_FILE_WRITE, file);
    Cli_DisplayMgmtAPI_Error(status);
    if (size) {
      xfree(buf);
    }
    return status;
  }

  if (size) {
    xfree(buf);
  }

  Cli_Printf("Successfully updated config file.\n");

  return status;
}

// enable recent configuration changes by performing the action specified
// by the action_need value
INKError
Cli_ConfigEnactChanges(INKActionNeedT action_need)
{
  INKError status;

  Cli_Debug("Cli_ConfigEnactChanges: action_need %d\n", action_need);

  switch (action_need) {
  case INK_ACTION_SHUTDOWN:
    Cli_Debug("Cli_ConfigEnactChanges: INK_ACTION_SHUTDOWN\n");
    Cli_Printf("\nHard Restart required.\n"
               "  Change will take effect after next Hard Restart.\n"
               "  Use the \"config:hard-restart\" command to restart now.\n\n");
    break;

  case INK_ACTION_RESTART:
    Cli_Debug("Cli_ConfigEnactChanges: INK_ACTION_RESTART\n");
    Cli_Printf("\nRestart required.\n"
               "  Change will take effect after next Restart.\n"
               "  Use the \"config:restart\" command to restart now.\n\n");
    break;

  case INK_ACTION_DYNAMIC:
    Cli_Debug("Cli_ConfigEnactChanges: INK_ACTION_DYNAMIC\n");
    // no additional action required
    break;

  case INK_ACTION_RECONFIGURE:
    Cli_Debug("Cli_ConfigEnactChanges: INK_ACTION_RECONFIGURE\n");
    status = INKActionDo(INK_ACTION_RECONFIGURE);
    if (status) {
      Cli_Error("\nERROR %d: Failed to reread configuration files.\n\n", status);
      return INK_ERR_FAIL;
    }
    break;

  default:
    Cli_Debug("  Status Message #%d\n", action_need);
    Cli_Error("\nYou may need to use the \"config:hard-restart\" command\n" "to enable this configuration change.\n\n");
    return INK_ERR_OKAY;
  }

  return INK_ERR_OKAY;
}

// evaluate "stringval" and return 1 if "on", otherwise 0
int
Cli_EvalOnOffString(char *stringval)
{
  if (strcmp(stringval, "on") == 0) {
    return 1;
  }
  if (strcmp(stringval, "off") == 0) {
    return 0;
  }

  return -1;
}

////////////////////////////////////////////////////////////////
// Cli_RecordOnOff_Action
//
// used for records.config INT variables when 1 = on, 0 = off
//
// action = RECORD_GET retrieve and display the variable
//          RECORD_SET set the variable
//
// record = variable in records.config
//
// on_off = "on" mean 1, "off" mean 0
//
int
Cli_RecordOnOff_Action(int action, char *record, char *on_off)
{
  INKActionNeedT action_need;
  INKError status;
  INKInt int_val;

  switch (action) {
  case RECORD_SET:
    if (on_off) {
      if (!strcasecmp(on_off, "on")) {
        int_val = 1;
      } else if (!strcasecmp(on_off, "off")) {
        int_val = 0;
      } else {
        Cli_Error("Expected \"on\" or \"off\" but got %s\n", on_off);
        return CLI_ERROR;
      }
    } else {
      Cli_Error("Expected <on | off> but got nothing.\n");
      return CLI_ERROR;
    }
    status = Cli_RecordSetInt(record, int_val, &action_need);
    if (status != INK_ERR_OKAY) {
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));

  case RECORD_GET:
    int_val = -1;
    status = Cli_RecordGetInt(record, &int_val);
    Cli_PrintEnable("", int_val);
    return CLI_OK;
  }
  return CLI_ERROR;
}

////////////////////////////////////////////////////////////////
// Cli_RecordInt_Action
//
// used for records.config INT variables
//
// action = RECORD_GET retrieve and display the variable
//          RECORD_SET set the variable
//
// record = variable in records.config
//
// value = the integer value used by RECORD_SET
//
int
Cli_RecordInt_Action(int action, char *record, int value)
{
  switch (action) {
  case RECORD_SET:
    {
      INKActionNeedT action_need = INK_ACTION_UNDEFINED;
      INKError status = Cli_RecordSetInt(record, value, &action_need);

      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
  case RECORD_GET:
    {
      INKInt value_in = -1;
      INKError status = Cli_RecordGetInt(record, &value_in);

      if (status) {
        return status;
      }
      Cli_Printf("%d\n", value_in);
      return CLI_OK;
    }
  }
  return CLI_ERROR;
}

////////////////////////////////////////////////////////////////
// Cli_RecordHostname_Action
//
// used for records.config STRING variables
// performs checking to see if string is a valid fully qualified hostname
//
// action = RECORD_GET retrieve and display the variable
//          RECORD_SET set the variable
//
// record = variable in records.config
//
// hostname = string to set
//
int
Cli_RecordHostname_Action(int action, char *record, char *hostname)
{
  INKError status;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKString str_val = NULL;

  switch (action) {
  case RECORD_SET:
    if (IsValidFQHostname(hostname) == CLI_OK) {
      status = Cli_RecordSetString(record, (INKString) hostname, &action_need);

      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
    Cli_Error("ERROR: %s is an invalid name.\n", hostname);
    return CLI_ERROR;

  case RECORD_GET:
    status = Cli_RecordGetString(record, &str_val);
    if (status) {
      return status;
    }
    if (str_val)
      Cli_Printf("%s\n", str_val);
    else
      Cli_Printf("none\n");
    return CLI_OK;
  }
  return CLI_ERROR;
}

////////////////////////////////////////////////////////////////
// Cli_RecordString_Action
//
// used for records.config STRING variables
//
// action = RECORD_GET retrieve and display the variable
//          RECORD_SET set the variable
//
// record = variable in records.config
//
// string_val = string to set
//
int
Cli_RecordString_Action(int action, char *record, char *string_val)
{
  INKError status;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKString str_val = NULL;

  switch (action) {
  case RECORD_SET:
    status = Cli_RecordSetString(record, (INKString) string_val, &action_need);

    if (status) {
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));

  case RECORD_GET:
    status = Cli_RecordGetString(record, &str_val);
    if (status) {
      return status;
    }
    if (str_val)
      Cli_Printf("%s\n", str_val);
    else
      Cli_Printf("none\n");
    return CLI_OK;
  }
  return CLI_ERROR;
}

////////////////////////////////////////////////////////////////
// Cli_ConfigFileURL_Action
//
// used for config files other than records.config
//
// file = integer which specifies config file
//
// filename = config file name to display
//
// url = if non-NULL, update the file using contents of URL
//
int
Cli_ConfigFileURL_Action(INKFileNameT file, char *filename, const char *url)
{
  INKError status;
  // Retrieve  file from url

  if (url == NULL) {
    Cli_Printf("%s File Rules\n", filename);
    Cli_Printf("----------------------------\n");
    status = Cli_DisplayRules(file);
    return status;
  }
  Cli_Printf("Retrieve and Install %s file from url %s\n", filename, url);

  status = Cli_SetConfigFileFromUrl(file, url);

  return (status);
}

int
cliCheckIfEnabled(char *command)
{
  if (enable_restricted_commands == FALSE) {
    Cli_Error("\n%s is a restricted command only accessible from enable mode\n\n", command);
    return CLI_ERROR;
  }
  return CLI_OK;
}

int
GetTSDirectory(char *ts_path, size_t ts_path_len)
{
  FILE *fp;
  char *env_path;

  struct stat s;
  int err;

  if ((env_path = getenv("TS_ROOT"))) {
    ink_strncpy(ts_path, env_path, ts_path_len);
  } else {
    if ((fp = fopen(DEFAULT_TS_DIRECTORY_FILE, "r")) != NULL) {
      if (fgets(ts_path, ts_path_len, fp) == NULL) {
        fclose(fp);
        Cli_Error("\nInvalid contents in %s\n",DEFAULT_TS_DIRECTORY_FILE);
        Cli_Error(" Please set correct path in env variable TS_ROOT \n");
        return -1;
      }
      // strip newline if it exists
      int len = strlen(ts_path);
      if (ts_path[len - 1] == '\n') {
        ts_path[len - 1] = '\0';
      }
      // strip trailing "/" if it exists
      len = strlen(ts_path);
      if (ts_path[len - 1] == '/') {
        ts_path[len - 1] = '\0';
      }
      
      fclose(fp);
    } else {
      ink_strncpy(ts_path, PREFIX, ts_path_len);
    }
  }

  if ((err = stat(ts_path, &s)) < 0) {
    Cli_Error("unable to stat() TS PATH '%s': %d %d, %s\n", 
              ts_path, err, errno, strerror(errno));
    Cli_Error(" Please set correct path in env variable TS_ROOT \n");
    return -1;
  }

  return 0;
}

int
StopTrafficServer()
{
  char ts_path[512];
  char stop_ts[1024];

  if (GetTSDirectory(ts_path,sizeof(ts_path))) {
    return CLI_ERROR;
  }
  snprintf(stop_ts, sizeof(stop_ts), "%s/bin/stop_traffic_server", ts_path);
  if (system(stop_ts) == -1)
    return CLI_ERROR;

  return 0;
}

int
StartTrafficServer()
{
  char ts_path[512];
  char start_ts[1024];

  if (GetTSDirectory(ts_path,sizeof(ts_path))) {
    return CLI_ERROR;
  }
  // root user should start_traffic_shell as inktomi user
  if (getuid() == 0) {
    snprintf(start_ts, sizeof(start_ts), "/bin/su - inktomi -c \"%s/bin/start_traffic_server\"", ts_path);
  } else {
    snprintf(start_ts, sizeof(start_ts), "%s/bin/start_traffic_server", ts_path);
  }
  if (system(start_ts) == -1)
    return CLI_ERROR;

  return 0;
}

int
Cli_CheckPluginStatus(INKString plugin)
{

  int match = 0;
  INKCfgContext ctx;
  INKCfgIterState ctx_state;
  INKPluginEle *ele;

  ctx = INKCfgContextCreate(INK_FNAME_PLUGIN);
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY) {
    printf("ERROR READING FILE\n");
  }
  ele = (INKPluginEle *) INKCfgContextGetFirst(ctx, &ctx_state);

  while (ele) {
    if (!strcasecmp(plugin, ele->name)) {
      match = 1;
      break;
    }
    ele = (INKPluginEle *) INKCfgContextGetNext(ctx, &ctx_state);
  }

  if (match) {
    return CLI_OK;
  } else {
    return CLI_ERROR;
  }

}
