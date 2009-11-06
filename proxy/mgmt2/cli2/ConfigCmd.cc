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
 * Filename: ConfigCmd.cc
 * Purpose: This file contains the CLI's "config" command implementation.
 *
 *  createArgument("timezone",1,CLI_ARGV_OPTION_INT_VALUE,
		  (char*)NULL, CMD_CONFIG_TIMEZONE, "Time Zone",
                  (char*)NULL);
 ****************************************************************/

#include "../api2/include/INKMgmtAPI.h"
#include "ShowCmd.h"
#include "ConfigCmd.h"
#include "createArgument.h"
#include "CliMgmtUtils.h"
#include "CliDisplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "cli_scriptpaths.h"
#include "inktomi++.h"
#include <ConfigAPI.h>
#include <SysAPI.h>


int enable_restricted_commands = FALSE;

static int find_value(char *pathname, char *key, char *value, int value_len, char *delim, int no);

int
u_getch(void)
{
  static signed int returned = (-1), fd;
  static struct termios new_io_settings, old_io_settings;
  fd = fileno(stdin);
  tcgetattr(fd, &old_io_settings);
  new_io_settings = old_io_settings;
  new_io_settings.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(fd, TCSANOW, &new_io_settings);
  returned = getchar();
  tcsetattr(fd, TCSANOW, &old_io_settings);
  return returned;
}

int
cliVerifyPasswd(char *passwd)
{
  char *e_passwd = NULL;
  INKError status = INK_ERR_OKAY;
  INKString old_passwd = NULL;

  INKEncryptPassword(passwd, &e_passwd);
  status = Cli_RecordGetString("proxy.config.admin.admin_password", &old_passwd);

  if (status != INK_ERR_OKAY) {
    if (e_passwd)
      xfree(e_passwd);
    return CLI_ERROR;
  }
  if (e_passwd) {
    if (strcmp((char *) old_passwd, e_passwd)) {
      xfree(e_passwd);
      return CLI_ERROR;
    }
    xfree(e_passwd);
  } else {

    return CLI_ERROR;

  }


  return CLI_OK;
}

////////////////////////////////////////////////////////////////
// Cmd_Enable
//
// This is the callback
// function for the "enable" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_Enable(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */

  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  /* Add "enable status" to check the status of enable/disable */
  if (argc == 2) {
    switch (infoPtr->parsed_args) {
    case CMD_ENABLE_STATUS:
      if (enable_restricted_commands == TRUE) {
        Cli_Printf("on\n");
        return CMD_OK;
      } else {
        Cli_Printf("off\n");
        return CMD_ERROR;
      }
    default:
      Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
      return CMD_ERROR;
    }
  }

  if (enable_restricted_commands == TRUE) {
    Cli_Printf("Already Enabled\n");
    return CMD_OK;
  }
  char passwd[256], ch = 'p';
  int i = 0;
  printf("Password:");
  fflush(stdout);
  ch = u_getch();
  while (ch != '\n' && ch != '\r') {
    passwd[i] = ch;
    i++;
    ch = u_getch();

  }
  passwd[i] = 0;
  if (cliVerifyPasswd(passwd) == CLI_ERROR) {
    Cli_Printf("\nIncorrect Password\n");
    return CMD_ERROR;
  }
  Cli_Printf("\n");
  enable_restricted_commands = TRUE;
  return CMD_OK;
}

////////////////////////////////////////////////////////////////
// CmdArgs_Enable
//
// Register "enable" arguments with the Tcl interpreter.
//
int
CmdArgs_Enable()
{

  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_ENABLE_STATUS, "Check Enable Status", (char *) NULL);
  return CLI_OK;
}

////////////////////////////////////////////////////////////////
// Cmd_Disable
//
// This is the callback
// function for the "disable" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_Disable(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (getuid() == 0) {
    Cli_Printf("root user cannot \"disable\"\n");
    return 0;
  }

  enable_restricted_commands = FALSE;
  return 0;
}



////////////////////////////////////////////////////////////////
// Cmd_Config
//
// This is the callback function for the "config" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_Config(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  char *cmdinfo, *temp;
  int i = 0;
  Cli_Debug("Cmd_Config\n");
  Tcl_Eval(interp, "info commands config* ");

  size_t cmdinfo_len = strlen(interp->result) + 2;
  cmdinfo = (char *) malloc(sizeof(char) * cmdinfo_len);
  ink_strncpy(cmdinfo, interp->result, cmdinfo_len);
  size_t temp_len = strlen(cmdinfo) + 20;
  temp = (char *) malloc(sizeof(char) * temp_len);
  ink_strncpy(temp, "lsort \"", temp_len);
  strncat(temp, cmdinfo, temp_len - strlen(temp));
  strncat(temp, "\"", temp_len - strlen(temp));
  Tcl_Eval(interp, temp);
  ink_strncpy(cmdinfo, interp->result, cmdinfo_len);
  i = i + strlen("config ");
  while (cmdinfo[i] != 0) {
    if (cmdinfo[i] == ' ') {
      cmdinfo[i] = '\n';
    }
    i++;
  }
  cmdinfo[i] = '\n';
  i++;
  cmdinfo[i] = 0;
  Cli_Printf("Following are the available config commands\n");
  Cli_Printf(cmdinfo + strlen("config "));
  free(cmdinfo);
  free(temp);
  return CLI_OK;

}


////////////////////////////////////////////////////////////////
// CmdArgs_Config
//
// Register "config" command arguments with the Tcl interpreter.
//
int
CmdArgs_Config()
{
  Cli_Debug("CmdArgs_Config\n");

  return CLI_OK;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigGet
//
// This is the callback function for the "config:get" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigGet(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:get") == CLI_ERROR) {
    return CMD_ERROR;
  }
  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  Cli_Debug("Cmd_ConfigGet argc %d\n", argc);

  if (argc == 2) {
    return (ConfigGet(argv[1]));
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigSet
//
// This is the callback function for the "config:set" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigSet(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:set") == CLI_ERROR) {
    return CMD_ERROR;
  }
  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  Cli_Debug("Cmd_ConfigSet argc %d\n", argc);

  if (argc == 3) {
    return (ConfigSet(argv[1], argv[2]));
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigName
//
// This is the callback function for the "config:name" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigName(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:name") == CLI_ERROR) {
    return CMD_ERROR;
  }
  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  Cli_Debug("Cmd_ConfigName argc %d\n", argc);

  return (ConfigName(argv[1]));

  // coverity[unreachable]
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigStart
//
// This is the callback function for the "config:start" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigStart(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:start") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  Cli_Debug("Cmd_ConfigStart argc %d\n", argc);

  if (argc == 1) {
    return (ConfigStart());
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigStop
//
// This is the callback function for the "config:stop" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigStop(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;
  if (cliCheckIfEnabled("config:stop") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;
  Cli_Debug("Cmd_ConfigStop argc %d\n", argc);

  if (argc == 1) {
    return (ConfigStop());
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigHardRestart
//
// This is the callback function for the "config:hard-restart" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigHardRestart(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;
  if (cliCheckIfEnabled("config:hard-restart") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;
  Cli_Debug("Cmd_ConfigHardRestart argc %d\n", argc);

  if (argc == 1) {
    return (INKHardRestart());
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigRestart
//
// This is the callback function for the "config:restart" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigRestart(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;
  if (cliCheckIfEnabled("config:restart") == CLI_ERROR) {
    return CMD_ERROR;
  }

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;
  Cli_Debug("Cmd_ConfigRestart argc %d\n", argc);

  if (argc == 1) {
    return (INKRestart(false));
  } else if (argc == 2) {
    if (argtable[0].parsed_args == CMD_CONFIG_RESTART_CLUSTER) {
      return (INKRestart(true));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigRestart
//
// Register "config:restart" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigRestart()
{
  createArgument("cluster", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_RESTART_CLUSTER, "Restart the entire cluster", (char *) NULL);

  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigFilter
//
// This is the callback function for the "config:filter" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigFilter(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:filter") == CLI_ERROR) {
    return CMD_ERROR;
  }
  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  Cli_Debug("Cmd_ConfigFilter argc %d\n", argc);

  if (argc == 2) {
    return (ConfigFilter(argv[1]));
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigParents
//
// This is the callback function for the "config:parents" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigParents(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:parents") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigParents argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  if (argc == 1) {
    Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
    return CMD_ERROR;
  }

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (infoPtr->parsed_args) {
    case CMD_CONFIG_PARENTS_STATUS:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.http.parent_proxy_routing_enable", argtable->arg_string));

    case CMD_CONFIG_PARENTS_CACHE:
      return (Cli_RecordString_Action((argc == 3), "proxy.config.http.parent_proxies", argtable->arg_string));

    case CMD_CONFIG_PARENTS_CONFIG_FILE:
      return (Cli_ConfigFileURL_Action(INK_FNAME_PARENT_PROXY, "parent.config", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigParents
//
// Register "config:parents" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigParents()
{

  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_PARENTS_STATUS, "Parenting <on|off>", (char *) NULL);
  createArgument("name", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_PARENTS_CACHE, "Specify cache parent", (char *) NULL);
  createArgument("rules", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_PARENTS_CONFIG_FILE, "Specify config file", (char *) NULL);
  return CLI_OK;
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigRemap
//
// This is the callback function for the "config:remap" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigRemap(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:remap") == CLI_ERROR) {
    return CMD_ERROR;
  }
  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  Cli_Debug("Cmd_ConfigRemap argc %d\n", argc);

  if (argc == 2) {
    return (ConfigRemap(argv[1]));
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigPorts
//
// This is the callback function for the "config:ports" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigPorts(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:ports") == CLI_ERROR) {
    return CMD_ERROR;
  }

  Cli_Debug("Cmd_ConfigPorts argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;


  Cli_Debug("Cmd_ConfigPorts argc %d\n", argc);

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {

    if (argc == 2) {            // get
      return (ConfigPortsGet(argtable[0].parsed_args));
    } else {                    // set
      switch (argtable->parsed_args) {
      case CMD_CONFIG_PORTS_HTTP_OTHER:
      case CMD_CONFIG_PORTS_SSL:
        return (ConfigPortsSet(argtable[0].parsed_args, argtable[0].data));
        break;
      case CMD_CONFIG_PORTS_HTTP_SERVER:
      case CMD_CONFIG_PORTS_WEBUI:
      case CMD_CONFIG_PORTS_OVERSEER:
      case CMD_CONFIG_PORTS_CLUSTER:
      case CMD_CONFIG_PORTS_CLUSTER_RS:
      case CMD_CONFIG_PORTS_CLUSTER_MC:
      case CMD_CONFIG_PORTS_NNTP_SERVER:
      case CMD_CONFIG_PORTS_FTP_SERVER:
      case CMD_CONFIG_PORTS_SOCKS_SERVER:
      case CMD_CONFIG_PORTS_ICP:
        return (ConfigPortsSet(argtable[0].parsed_args, &argtable[0].arg_int));
      }
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX,
            "\n\nconfig:ports <http-server | http-other | webui | \n overseer | cluster-rs | cluster-mc | \n nntp-server | ftp-server | ssl | \n socks-server | icp > \n <port | ports list>\n");
  return CMD_ERROR;

}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigPorts
//
// Register "config:ports" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigPorts()
{
  createArgument("http-server", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_HTTP_SERVER, "Set Ports for http-server", (char *) NULL);
  createArgument("http-other", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_HTTP_OTHER, "Set Ports for http-other", (char *) NULL);
  createArgument("webui", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_WEBUI, "Set Ports for webui", (char *) NULL);
  createArgument("overseer", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_OVERSEER, "Set Ports for overseer", (char *) NULL);
  createArgument("cluster", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_CLUSTER, "Set Ports for cluster", (char *) NULL);
  createArgument("cluster-rs", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_CLUSTER_RS, "Set Ports for cluster-rs", (char *) NULL);
  createArgument("cluster-mc", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_CLUSTER_MC, "Set Ports for cluster-mc", (char *) NULL);
  createArgument("nntp-server", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_NNTP_SERVER, "Set Ports for nntp-server", (char *) NULL);
  createArgument("ftp-server", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_FTP_SERVER, "Set Ports for ftp-server", (char *) NULL);
  createArgument("ssl", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_SSL, "Set Ports for ssl", (char *) NULL);
  createArgument("socks-server", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORTS_SOCKS_SERVER, "Set Ports for socks-server", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigSnmp
//
// This is the callback function for the "config:snmp" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigSnmp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:snmp") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigSnmp argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    if (argtable->parsed_args == CMD_CONFIG_SNMP_STATUS) {
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.snmp.master_agent_enabled", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSnmp
//
// Register "config:snmp" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigSnmp()
{
  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SNMP_STATUS, "SNMP <on | off>", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigLdap
//
// This is the callback function for the "config:ldap" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigLdap(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:ldap") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigLdap argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  Cli_PrintArg(0, argtable);
  Cli_PrintArg(1, argtable);

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_LDAP_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.ldap.auth.enabled", argtable->arg_string));

    case CMD_CONFIG_LDAP_CACHE_SIZE:
      return (Cli_RecordInt_Action(action, "proxy.config.ldap.cache.size", argtable->arg_int));

    case CMD_CONFIG_LDAP_TTL:
      return (Cli_RecordInt_Action(action, "proxy.config.ldap.auth.ttl_value", argtable->arg_int));

    case CMD_CONFIG_LDAP_PURGE_FAIL:
      return (Cli_RecordOnOff_Action(action, "proxy.config.ldap.auth.purge_cache_on_auth_fail", argtable->arg_string));

    case CMD_CONFIG_LDAP_SERVER_NAME:
      return (Cli_RecordOnOff_Action(action, "proxy.config.ldap.proc.ldap.server.name", argtable->arg_string));

    case CMD_CONFIG_LDAP_SERVER_PORT:
      return (Cli_RecordOnOff_Action(action, "proxy.config.ldap.proc.ldap.server.port", argtable->arg_string));

    case CMD_CONFIG_LDAP_DN:
      return (Cli_RecordOnOff_Action(action, "proxy.config.ldap.proc.ldap.base.dn", argtable->arg_string));

    case CMD_CONFIG_LDAP_FILE:
      return (Cli_ConfigFileURL_Action(INK_FNAME_FILTER, "filter.config", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigLdap
//
// Register "config:ldap" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigLdap()
{

  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_LDAP_STATUS, "Set ldap status <on | off>", (char *) NULL);
  createArgument("cache-size", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_LDAP_CACHE_SIZE, "Set cache size <int>", (char *) NULL);
  createArgument("ttl", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_LDAP_TTL, "Set ttl value <int>", (char *) NULL);
  createArgument("purge-fail", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_LDAP_PURGE_FAIL, "Set purge fail <on | off>", (char *) NULL);
  createArgument("server-name", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_LDAP_SERVER_NAME, "Set Server Name <string>", (char *) NULL);
  createArgument("server-port", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_LDAP_SERVER_PORT, "Set Server Port <int>", (char *) NULL);
  createArgument("dn", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_LDAP_DN, "Set DN", (char *) NULL);
  createArgument("file", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_LDAP_FILE, "Set filter.config (used for LDAP configuration) from <url>",
                 (char *) NULL);
  return 0;
}



////////////////////////////////////////////////////////////////
// Cmd_ConfigNNTP
//
// This is the callback function for the "config:nntp" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigNNTP(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  INKString plugin_name = NULL;
  INKError status = INK_ERR_OKAY;
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:nntp") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigNNTP argc %d\n", argc);
  status = Cli_RecordGetString("proxy.config.nntp.plugin_name", &plugin_name);
  if (status != INK_ERR_OKAY) {
    return status;
  }
  if (Cli_CheckPluginStatus(plugin_name) != CLI_OK) {
    Cli_Printf("NNTP is not installed.\n\n");
    return CMD_ERROR;
  };

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  if (argc == 1) {
    Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
    return CMD_ERROR;
  }

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (infoPtr->parsed_args) {
    case CMD_CONFIG_NNTP_STATUS:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.enabled", argtable->arg_string));
#if 0
    case CMD_CONFIG_NNTP_PORT:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.server_port", argtable->arg_int));

    case CMD_CONFIG_NNTP_CONNECTMSG:
      infoPtr++;
      if (ConfigNNTPConnectmsg(infoPtr->parsed_args, infoPtr->arg_string) == CLI_ERROR) {
        infoPtr--;
      }
      break;

    case CMD_CONFIG_NNTP_POSTINGSTATUS:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.posting_enabled", argtable->arg_string));

    case CMD_CONFIG_NNTP_ACCESSCONTROL:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.access_control_enabled", argtable->arg_string));

    case CMD_CONFIG_NNTP_v2AUTH:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.v2_authentication", argtable->arg_string));

    case CMD_CONFIG_NNTP_LOCALAUTH:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.nntp.run_local_authentication_server", argtable->arg_string));

    case CMD_CONFIG_NNTP_CLUSTERING:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.cluster_enabled", argtable->arg_string));

    case CMD_CONFIG_NNTP_ALLOWFEEDS:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.feed_enabled", argtable->arg_string));

    case CMD_CONFIG_NNTP_ACCESSLOGS:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.logging_enabled", argtable->arg_string));

    case CMD_CONFIG_NNTP_BACKPOSTING:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.nntp.background_posting_enabled", argtable->arg_string));
#endif
    case CMD_CONFIG_NNTP_OBEYCANCEL:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.obey_control_cancel", argtable->arg_string));

    case CMD_CONFIG_NNTP_OBEYNEWGROUPS:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.obey_control_newgroup", argtable->arg_string));

    case CMD_CONFIG_NNTP_OBEYRMGROUPS:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.obey_control_rmgroup", argtable->arg_string));

    case CMD_CONFIG_NNTP_INACTIVETIMEOUT:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.inactivity_timeout", argtable->arg_int));

    case CMD_CONFIG_NNTP_CHECKNEWGROUPS:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.check_newgroups_every", argtable->arg_int));

    case CMD_CONFIG_NNTP_CHECKCANCELLED:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.check_cancels_every", argtable->arg_int));

    case CMD_CONFIG_NNTP_CHECKPARENT:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.group_check_parent_every", argtable->arg_int));
#if 0
    case CMD_CONFIG_NNTP_CHECKCLUSTER:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.group_check_cluster_every", argtable->arg_int));
#endif
    case CMD_CONFIG_NNTP_CHECKPULL:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.check_pull_every", argtable->arg_int));
#if 0
    case CMD_CONFIG_NNTP_AUTHSERVER:
      return (Cli_RecordString_Action((argc == 3), "proxy.config.nntp.authorization_hostname", argtable->arg_string));

    case CMD_CONFIG_NNTP_AUTHPORT:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.authorization_port", argtable->arg_int));

    case CMD_CONFIG_NNTP_AUTHTIMEOUT:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.authorization_server_timeout", argtable->arg_int));

    case CMD_CONFIG_NNTP_CLIENTTHROTTLE:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.nntp.client_speed_throttle", argtable->arg_int));
    case CMD_CONFIG_NNTP_SERVERS:
      return (Cli_ConfigFileURL_Action(INK_FNAME_NNTP_SERVERS, "nntp_servers.config", argtable->arg_string));

    case CMD_CONFIG_NNTP_ACCESS:
      return (Cli_ConfigFileURL_Action(INK_FNAME_NNTP_ACCESS, "nntp_servers.config", argtable->arg_string));
#endif
    case CMD_CONFIG_NNTP_CONFIG_XML:
      return (Cli_ConfigFileURL_Action(INK_FNAME_NNTP_CONFIG_XML, "nntp-config.xml", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

int
ConfigNNTPConnectmsg(int option, char *string)
{
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;
  INKString str_val = NULL;
  char buf[256];

  Cli_Debug("ConfigNNTPConnectmsg: posting | non-posting %s\n", string);
  if (option == CMD_CONFIG_NNTP_POSTING) {
    if (string) {
      status = Cli_RecordSetString("proxy.config.nntp.posting_ok_message", (INKString) string, &action_need);
      Cli_Printf("connect-msg posting set to %s\n", string);
    } else {
      status = Cli_RecordGetString("proxy.config.nntp.posting_ok_message", &str_val);
      Cli_Printf("%s\n", (char *) str_val);
    }
  } else if (option == CMD_CONFIG_NNTP_NONPOSTING) {
    if (string) {
      status = Cli_RecordSetString("proxy.config.nntp.posting_not_ok_message", (INKString) string, &action_need);
      Cli_Printf("connect-msg non-posting set to %s\n", string);
    } else {
      status = Cli_RecordGetString("proxy.config.nntp.posting_not_ok_message", &str_val);
      Cli_Printf("%s\n", (char *) str_val);
    }
  } else {
    snprintf(buf, sizeof(buf), "config nntp connect-msg <posting | non-posting> <string>\n");
    Cli_Printf(buf);
    return CLI_ERROR;
  }
  if (status != INK_ERR_OKAY) {
    Cli_ConfigEnactChanges(action_need);
    return CLI_ERROR;
  }

  return CLI_OK;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigNNTP
//
// Register "config:nntp" arguments with the Tcl interpreter.
//

int
CmdArgs_ConfigNNTP()
{
  createArgument("status", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_STATUS, "Set NNTP <on|off>", (char *) NULL);
#if 0
  createArgument("port", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_PORT, "Port <int>", (char *) NULL);

  createArgument("connect-msg", CLI_ARGV_NO_POS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_NNTP_CONNECTMSG, "connect-msg <posting | non-posting> <string>",
                 (char *) NULL);
  createArgument("posting", CMD_CONFIG_NNTP_CONNECTMSG, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_POSTING, "connect-msg posting <string>", (char *) NULL);
  createArgument("non-posting", CMD_CONFIG_NNTP_CONNECTMSG, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_NONPOSTING, "connect-msg non-posting <string>", (char *) NULL);

  createArgument("posting-status", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_POSTINGSTATUS, "Posting <on|off>", (char *) NULL);

  createArgument("access-control", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_ACCESSCONTROL, "Access Control <on|off>", (char *) NULL);

  createArgument("v2-auth", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_v2AUTH, "NNTP v2 Auth <on|off>", (char *) NULL);

  createArgument("local-auth", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_LOCALAUTH, "Local Auth <on|off>", (char *) NULL);

  createArgument("clustering", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_CLUSTERING, "Clustering <on|off>", (char *) NULL);

  createArgument("allow-feeds", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_ALLOWFEEDS, "Allow Feeds <on|off>", (char *) NULL);

  createArgument("access-logs", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_ACCESSLOGS, "Access Logs <on|off>", (char *) NULL);

  createArgument("background-posting", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_BACKPOSTING, "Background Posting <on|off>", (char *) NULL);
#endif
  createArgument("obey-cancel", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_OBEYCANCEL, "Obey Cancel <on|off>", (char *) NULL);

  createArgument("obey-newgroups", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_OBEYNEWGROUPS, "Obey Newgroups <on|off>", (char *) NULL);

  createArgument("obey-rmgroups", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_OBEYRMGROUPS, "Obey Rmgroups <on|off>", (char *) NULL);

  createArgument("inactive-timeout", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_INACTIVETIMEOUT, "Inactive Timeout <seconds>", (char *) NULL);

  createArgument("check-new-groups", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_CHECKNEWGROUPS, "Check New Groups interval <seconds>", (char *) NULL);

  createArgument("check-cancelled", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_CHECKCANCELLED, "Check Cancelled interval <seconds>", (char *) NULL);

  createArgument("check-parent", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_CHECKPARENT, "Check Parent interval <seconds>", (char *) NULL);

  createArgument("check-pull", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_CHECKPULL, "Check Pull interval <seconds>", (char *) NULL);
#if 0
  createArgument("check-cluster", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_CHECKCLUSTER, "Check Cluster interval <seconds>", (char *) NULL);

  createArgument("check-pull", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_CHECKPULL, "Check Pull interval <seconds>", (char *) NULL);

  createArgument("auth-server", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_AUTHSERVER, "Auth Server <string>", (char *) NULL);

  createArgument("auth-port", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_AUTHPORT, "Auth Port <int>", (char *) NULL);

  createArgument("auth-timeout", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_AUTHTIMEOUT, "Auth Timeout <seconds>", (char *) NULL);

  createArgument("client-throttle", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_CLIENTTHROTTLE,
                 "Client Throttle <bytes per second | 0>", (char *) NULL);

  createArgument("nntp-servers", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_SERVERS, "Update NNTP Servers config file <url>", (char *) NULL);

  createArgument("nntp-access", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_ACCESS, "Update NNTP Access config file <url>", (char *) NULL);
#endif
  createArgument("config-xml", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NNTP_CONFIG_XML, "Update nntp config file <url>", (char *) NULL);
  return 0;


}


////////////////////////////////////////////////////////////////
// Cmd_ConfigClock
//
// This is the callback function for the "config:clock" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigClock(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  int setvar = 0;

  if (cliCheckIfEnabled("config:clock") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigClock argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  Cli_PrintArg(0, argtable);
  Cli_PrintArg(1, argtable);

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable[0].parsed_args) {
    case CMD_CONFIG_DATE:
      return (ConfigDate(argtable->arg_string));
    case CMD_CONFIG_TIME:
      return (ConfigTime(argtable->arg_string));
    case CMD_CONFIG_TIMEZONE:
      if (argc == 3) {
        setvar = 1;
      }
      if (argtable[1].parsed_args == CMD_CONFIG_TIMEZONE_LIST) {
        return (ConfigTimezoneList());
      } else {
        return (ConfigTimezone(argtable->arg_int, setvar));
      }
    }
  }

  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigClock
//
// Register "config:clock" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigClock()
{
  createArgument("date", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_DATE, "System Date <mm/dd/yyyy>", (char *) NULL);

  createArgument("time", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_TIME, "System Time <hh:mm:ss>", (char *) NULL);

  createArgument("timezone", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_TIMEZONE, "Time Zone", (char *) NULL);

  createArgument("list", CMD_CONFIG_TIMEZONE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_TIMEZONE_LIST, "Display Time Zone List", (char *) NULL);

  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigSecurity
//
// This is the callback function for the "config:security" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigSecurity(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:security") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigSecurity argc %d\n", argc);


  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  if (argtable[0].parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable[0].parsed_args) {
    case CMD_CONFIG_SECURITY_IP:
      return (Cli_ConfigFileURL_Action(INK_FNAME_IP_ALLOW, "ip_allow.config", argtable->arg_string));

    case CMD_CONFIG_SECURITY_MGMT:
      return (Cli_ConfigFileURL_Action(INK_FNAME_MGMT_ALLOW, "mgmt_allow.config", argtable->arg_string));

    case CMD_CONFIG_SECURITY_ADMIN:
      return (Cli_ConfigFileURL_Action(INK_FNAME_ADMIN_ACCESS, "admin_access.config", argtable->arg_string));

    case CMD_CONFIG_SECURITY_PASSWORD:
      return (ConfigSecurityPasswd());
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSecurity
//
// Register "config:security" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigSecurity()
{
  createArgument("ip-allow", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SECURITY_IP, "Clients allowed to connect to proxy <url>", (char *) NULL);
  createArgument("mgmt-allow", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SECURITY_MGMT, "Clients allowed to connect to manager <url>", (char *) NULL);
  createArgument("admin", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SECURITY_ADMIN, "Administrator access to WebUI <url>", (char *) NULL);

  createArgument("password", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_SECURITY_PASSWORD, "Change Admin Password", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigHttp
//
// This is the callback function for the "config:http" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigHttp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  int setvar = 0;
  if (cliCheckIfEnabled("config:http") == CLI_ERROR) {
    return CMD_ERROR;
  }

  Cli_Debug("Cmd_ConfigHttp argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  if (argc == 3) {
    setvar = 1;
  }

  if (argc > 3) {
    Cli_Error("Too many arguments\n");
    Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
    return CMD_ERROR;
  }

  Cli_PrintArg(0, argtable);
  Cli_PrintArg(1, argtable);

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_HTTP_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.cache.http", argtable->arg_string));

    case CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_IN:
      return (Cli_RecordInt_Action(action, "proxy.config.http.keep_alive_no_activity_timeout_in", argtable->arg_int));

    case CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_OUT:
      return (Cli_RecordInt_Action(action, "proxy.config.http.keep_alive_no_activity_timeout_out", argtable->arg_int));

    case CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_IN:
      return (Cli_RecordInt_Action(action, "proxy.config.http.transaction_no_activity_timeout_in", argtable->arg_int));

    case CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_OUT:
      return (Cli_RecordInt_Action(action, "proxy.config.http.transaction_no_activity_timeout_out", argtable->arg_int));

    case CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_IN:
      return (Cli_RecordInt_Action(action, "proxy.config.http.transaction_active_timeout_in", argtable->arg_int));

    case CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_OUT:
      return (Cli_RecordInt_Action(action, "proxy.config.http.transaction_active_timeout_out", argtable->arg_int));

    case CMD_CONFIG_HTTP_REMOVE_FROM:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_from", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_REFERER:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_referer", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_USER:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_user_agent", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_COOKIE:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_cookie", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_HEADER:
      return (Cli_RecordString_Action(action, "proxy.config.http.anonymize_other_header_list", argtable->arg_string));

    case CMD_CONFIG_HTTP_GLOBAL_USER_AGENT:
      return (Cli_RecordString_Action(action, "proxy.config.http.global_user_agent_header", argtable->arg_string));

    case CMD_CONFIG_HTTP_INSERT_IP:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_insert_client_ip", argtable->arg_string));

    case CMD_CONFIG_HTTP_REMOVE_IP:
      return (Cli_RecordOnOff_Action(action, "proxy.config.http.anonymize_remove_client_ip", argtable->arg_string));

    case CMD_CONFIG_HTTP_PROXY:
      return (ConfigHttpProxy(argtable[1].parsed_args, setvar));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigHttp
//
// Register "config:http" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigHttp()
{
  createArgument("status", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_STATUS, "HTTP proxying <on | off>", (char *) NULL);

  createArgument("keep-alive-timeout-in", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_IN, "Keep alive timeout inbound <seconds>",
                 (char *) NULL);
  createArgument("keep-alive-timeout-out", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_KEEP_ALIVE_TIMEOUT_OUT, "Keep alive timeout outbound <seconds>",
                 (char *) NULL);
  createArgument("inactive-timeout-in", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_IN, "Inactive timeout inbound <seconds>",
                 (char *) NULL);
  createArgument("inactive-timeout-out", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_INACTIVE_TIMEOUT_OUT, "Inactive timeout outbound <seconds>",
                 (char *) NULL);
  createArgument("active-timeout-in", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_IN, "Active timeout inbound <seconds>", (char *) NULL);
  createArgument("active-timeout-out", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_ACTIVE_TIMEOUT_OUT, "Active timeout outbound <seconds>", (char *) NULL);

  createArgument("remove-from", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_FROM, "Remove \"From:\" header <on|off>", (char *) NULL);
  createArgument("remove-referer", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_REFERER, "Remove \"Referer:\" header <on|off>", (char *) NULL);
  createArgument("remove-user", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_USER, "Remove \"User:\" header <on|off>", (char *) NULL);
  createArgument("remove-cookie", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_COOKIE, "Remove \"Cookie:\" header <on|off>", (char *) NULL);
  createArgument("remove-header", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_HEADER, "String of headers to be removed <string>",
                 (char *) NULL);

  createArgument("global-user-agent", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_GLOBAL_USER_AGENT, "User-Agent to send to Origin <string>",
                 (char *) NULL);

  createArgument("insert-ip", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_INSERT_IP, "Insert client IP into header <on|off>", (char *) NULL);
  createArgument("remove-ip", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HTTP_REMOVE_IP, "Remove client IP from header <on|off>", (char *) NULL);
  createArgument("proxy", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_HTTP_PROXY, "Proxy Mode <fwd | rev | fwd-rev>", (char *) NULL);
  createArgument("fwd", CMD_CONFIG_HTTP_PROXY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_HTTP_FWD, "Specify proxy mode to be forward", (char *) NULL);
  createArgument("rev", CMD_CONFIG_HTTP_PROXY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_HTTP_REV, "Specify proxy mode to be reverse", (char *) NULL);
  createArgument("fwd-rev", CMD_CONFIG_HTTP_PROXY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_HTTP_FWD_REV, "Specify proxy mode to be both", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigFtp
//
// This is the callback function for the "config:ftp" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigFtp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:ftp") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigFtp argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;


  int setvar = 0;

  if (argc == 3) {
    setvar = 1;
  }

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_FTP_MODE:
      return (ConfigFtpMode(argtable[1].parsed_args, setvar));
    case CMD_CONFIG_FTP_INACT_TIMEOUT:
      return (ConfigFtpInactTimeout(argtable->arg_int, setvar));
    case CMD_CONFIG_FTP_ANON_PASSWD:
      return (Cli_RecordString_Action(setvar, "proxy.config.http.ftp.anonymous_passwd", argtable->arg_string));
    case CMD_CONFIG_FTP_EXPIRE_AFTER:
      return (ConfigFtpExpireAfter(argtable->arg_int, setvar));
    case CMD_CONFIG_FTP_PROXY:
      return (ConfigFtpProxy(argtable[1].parsed_args, setvar));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigFtp
//
// Register "config:ftp" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigFtp()
{
  createArgument("mode", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_FTP_MODE, "FTP Mode <pasv-port | port | pasv>", (char *) NULL);

  createArgument("pasv-port", CMD_CONFIG_FTP_MODE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_FTP_MODE_PASVPORT, "Specify ftp mode to be pasv-port", (char *) NULL);

  createArgument("pasv", CMD_CONFIG_FTP_MODE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_FTP_MODE_PASV, "Specify ftp mode to be pasv", (char *) NULL);

  createArgument("port", CMD_CONFIG_FTP_MODE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_FTP_MODE_PORT, "Specify ftp mode to be port", (char *) NULL);

  createArgument("inactivity-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_FTP_INACT_TIMEOUT, "Inactive timeout <seconds>", (char *) NULL);
  createArgument("anonymous-password", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_FTP_ANON_PASSWD, "FTP password <string>", (char *) NULL);
  createArgument("expire-after", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_FTP_EXPIRE_AFTER, "Expire after value <seconds>", (char *) NULL);
  createArgument("proxy", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_FTP_PROXY, "Proxy Mode <fwd | rev | fwd-rev>", (char *) NULL);
  createArgument("fwd", CMD_CONFIG_FTP_PROXY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_FTP_FWD, "Specify proxy mode to be forward", (char *) NULL);
  createArgument("rev", CMD_CONFIG_FTP_PROXY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_FTP_REV, "Specify proxy mode to be reverse", (char *) NULL);
  createArgument("fwd-rev", CMD_CONFIG_FTP_PROXY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_FTP_FWD_REV, "Specify proxy mode to be both)", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigIcp
//
// This is the callback function for the "config:icp" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigIcp(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:icp") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigIcp argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_ICP_MODE:
      return (ConfigIcpMode(argtable[1].parsed_args, action));

    case CMD_CONFIG_ICP_PORT:
      return (Cli_RecordInt_Action(action, "proxy.config.icp.icp_port", argtable->arg_int));

    case CMD_CONFIG_ICP_MCAST:
      return (Cli_RecordOnOff_Action(action, "proxy.config.icp.multicast_enabled", argtable->arg_string));

    case CMD_CONFIG_ICP_QTIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.icp.query_timeout", argtable->arg_int));

    case CMD_CONFIG_ICP_PEERS:
      return (Cli_ConfigFileURL_Action(INK_FNAME_ICP_PEER, "icp.config", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigIcp
//
// Register "config:Icp" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigIcp()
{
  createArgument("mode", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_ICP_MODE, "Mode <disabled | receive | send-receive>", (char *) NULL);

  createArgument("receive", CMD_CONFIG_ICP_MODE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_ICP_MODE_RECEIVE, "Specify receive mode for icp", (char *) NULL);

  createArgument("send-receive", CMD_CONFIG_ICP_MODE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_ICP_MODE_SENDRECEIVE, "Specify send & receive mode for icp", (char *) NULL);

  createArgument("disabled", CMD_CONFIG_ICP_MODE, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_ICP_MODE_DISABLED, "icp mode disabled", (char *) NULL);

  createArgument("port", 1, CLI_ARGV_OPTION_INT_VALUE, (char *) NULL, CMD_CONFIG_ICP_PORT, "Port <int>", (char *) NULL);
  createArgument("multicast", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_ICP_MCAST, "Multicast <on|off>", (char *) NULL);
  createArgument("query-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_ICP_QTIMEOUT, "Query Timeout <seconds>", (char *) NULL);
  createArgument("peers", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_ICP_PEERS, "URL for ICP Peers config file <url>", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigPortTunnels
//
// This is the callback function for the "config:port-tunnels" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigPortTunnels(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:port-tunnel") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigPortTunnles argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_PORT_TUNNELS_SERVER_OTHER_PORTS:
      int status = Cli_RecordInt_Action((argc == 3),
                                        "proxy.config.http.server_other_ports",
                                        argtable->arg_int);
      Cli_Printf("\n");
      Cli_Printf("Use config:remap to add rules to the remap.config file as follows:\n");
      Cli_Printf("map tunnel://<proxy_ip>:<port_num>/tunnel://<dest_server>:<dest_port>\n");
      Cli_Printf("\n");
      return status;
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigPortTunnles
//
// Register "config:PortTunnles" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigPortTunnels()
{

  createArgument("server-other-ports", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_PORT_TUNNELS_SERVER_OTHER_PORTS, "Set the tunnel port number <int>",
                 (char *) NULL);
  return 0;
}



////////////////////////////////////////////////////////////////
// Cmd_ConfigScheduledUpdate
//
// This is the callback function for the "config:scheduled-update" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigScheduledUpdate(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:scheduled-update") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigScheduledUpdate argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_SCHEDULED_UPDATE_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.update.enabled", argtable->arg_string));

    case CMD_CONFIG_SCHEDULED_UPDATE_RETRY_COUNT:
      return (Cli_RecordInt_Action(action, "proxy.config.update.retry_count", argtable->arg_int));

    case CMD_CONFIG_SCHEDULED_UPDATE_RETRY_INTERVAL:
      return (Cli_RecordInt_Action(action, "proxy.config.update.retry_interval", argtable->arg_int));

    case CMD_CONFIG_SCHEDULED_UPDATE_MAX_CONCURRENT:
      return (Cli_RecordInt_Action(action, "proxy.config.update.concurrent_updates", argtable->arg_int));

    case CMD_CONFIG_SCHEDULED_UPDATE_FORCE_IMMEDIATE:
      return (Cli_RecordOnOff_Action(action, "proxy.config.update.force", argtable->arg_string));

    case CMD_CONFIG_SCHEDULED_UPDATE_RULES:
      return (Cli_ConfigFileURL_Action(INK_FNAME_UPDATE_URL, "update.config", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigScheduled-Update
//
// Register "config:Scheduled-Update" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigScheduledUpdate()
{
  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_STATUS, "Set scheduled-update status <on | off>",
                 (char *) NULL);
  createArgument("retry-count", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_RETRY_COUNT, "Set retry-count <int>", (char *) NULL);
  createArgument("retry-interval", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_RETRY_INTERVAL, "Set retry-interval <sec>", (char *) NULL);
  createArgument("max-concurrent", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_MAX_CONCURRENT, "Set maximum concurrent updates",
                 (char *) NULL);
  createArgument("force-immediate", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_FORCE_IMMEDIATE, "Set force-immediate <on | off>",
                 (char *) NULL);
  createArgument("rules", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SCHEDULED_UPDATE_RULES, "Update update.config file from url <string>",
                 (char *) NULL);

  return 0;
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigSocks
//
// This is the callback function for the "config:scheduled-update" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigSocks(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:socks") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigSocks argc %d\n", argc);
  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_SOCKS_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.socks.socks_needed", argtable->arg_string));

    case CMD_CONFIG_SOCKS_VERSION:
      return (Cli_RecordInt_Action(action, "proxy.config.socks.socks_version", argtable->arg_int));

    case CMD_CONFIG_SOCKS_DEFAULT_SERVERS:
      return (Cli_RecordString_Action(action, "proxy.config.socks.default_servers", argtable->arg_string));

    case CMD_CONFIG_SOCKS_ACCEPT:
      return (Cli_RecordOnOff_Action(action, "proxy.config.socks.accept_enabled", argtable->arg_string));

    case CMD_CONFIG_SOCKS_ACCEPT_PORT:
      return (Cli_RecordInt_Action(action, "proxy.config.socks.accept_port", argtable->arg_int));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigScheduled-Update
//
// Register "config:Scheduled-Update" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigSocks()
{
  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_STATUS, "Set socks status <on | off>", (char *) NULL);

  createArgument("version", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_VERSION, "Set version <int>", (char *) NULL);

  createArgument("default-servers", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_DEFAULT_SERVERS, "Set default-servers <string>", (char *) NULL);

  createArgument("accept", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_ACCEPT, "Set accept <on | off>", (char *) NULL);

  createArgument("accept-port", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SOCKS_ACCEPT_PORT, "Set server accept-port <int>", (char *) NULL);

  return 0;
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigCache
//
// This is the callback function for the "config:cache" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigCache(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:cache") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigCache argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;


  int action = 0;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_CACHE_HTTP:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.http.cache.http", argtable->arg_string));

    case CMD_CONFIG_CACHE_NNTP:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.nntp.cache_enabled", argtable->arg_string));

    case CMD_CONFIG_CACHE_FTP:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.http.cache.ftp", argtable->arg_string));

    case CMD_CONFIG_CACHE_IGNORE_BYPASS:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.http.cache.ignore_client_no_cache", argtable->arg_string));

    case CMD_CONFIG_CACHE_MAX_OBJECT_SIZE:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.cache.max_doc_size", argtable->arg_int));

    case CMD_CONFIG_CACHE_MAX_ALTERNATES:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.cache.limits.http.max_alts", argtable->arg_int));

    case CMD_CONFIG_CACHE_FILE:
      return (Cli_ConfigFileURL_Action(INK_FNAME_CACHE_OBJ, "cache.config", argtable->arg_string));

    case CMD_CONFIG_CACHE_FRESHNESS:
      if (argtable[1].parsed_args != CLI_PARSED_ARGV_END) {
        switch (argtable[1].parsed_args) {
        case CMD_CONFIG_CACHE_FRESHNESS_VERIFY:
          if (argc == 4) {
            action = RECORD_SET;
          }
          return (ConfigCacheFreshnessVerify(argtable[2].parsed_args, action));

        case CMD_CONFIG_CACHE_FRESHNESS_MINIMUM:
          if (argc == 4) {
            action = RECORD_SET;
          }
          return (ConfigCacheFreshnessMinimum(argtable[2].parsed_args, action));

        case CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT:
          if (argtable[2].parsed_args != CLI_PARSED_ARGV_END) {
            if ((argtable[2].parsed_args ==
                 CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_GREATER_THAN) &&
                (argtable[3].parsed_args == CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_LESS_THAN) && (argc == 7)) {
              action = RECORD_SET;
            } else {
              Cli_Printf("\n config:cache freshness no-expire-limit greater-than <value> less-than<value>\n");
              return CMD_ERROR;
            }
          }
          Cli_Debug("greater than %d, less than %d \n", argtable[2].arg_int, argtable[3].arg_int);
          return (ConfigCacheFreshnessNoExpireLimit(argtable[2].arg_int, argtable[3].arg_int, action));

        }
      }
      Cli_Printf("\n config:cache freshness <verify | minimum | no-expire-limit> \n");
      return CMD_ERROR;
    case CMD_CONFIG_CACHE_DYNAMIC:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.http.cache.cache_urls_that_look_dynamic", argtable->arg_string));

    case CMD_CONFIG_CACHE_ALTERNATES:
      return (Cli_RecordOnOff_Action((argc == 3),
                                     "proxy.config.http.cache.enable_default_vary_headers", argtable->arg_string));

    case CMD_CONFIG_CACHE_VARY:
      if (argtable[1].arg_string) {
        action = RECORD_SET;
      }
      return (ConfigCacheVary(argtable[1].parsed_args, argtable[1].arg_string, action));

    case CMD_CONFIG_CACHE_COOKIES:
      if (argc == 3) {
        action = RECORD_SET;
      }
      return (ConfigCacheCookies(argtable[1].parsed_args, action));
    case CMD_CONFIG_CACHE_CLEAR:
      return (ConfigCacheClear());
    }
  }

  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigCache
//
// Register "config:cache" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigCache()
{
  createArgument("http", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_HTTP, "HTTP Protocol caching <on|off>", (char *) NULL);
  createArgument("nntp", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_NNTP, "NNTP Protocol caching <on|off>", (char *) NULL);
  createArgument("ftp", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_FTP, "FTP Protocol caching <on|off>", (char *) NULL);
  createArgument("ignore-bypass", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_IGNORE_BYPASS, "Ignore Bypass <on|off>", (char *) NULL);
  createArgument("max-object-size", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_MAX_OBJECT_SIZE, "Maximum object size <bytes>", (char *) NULL);
  createArgument("max-alternates", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_MAX_ALTERNATES, "Maximum alternates <int>", (char *) NULL);
  createArgument("file", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_FILE, "Load cache.config file from url <string>", (char *) NULL);
  createArgument("freshness", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS, "Freshness parameters <verify | minimum | no-expire-limit>",
                 (char *) NULL);
  createArgument("verify", CMD_CONFIG_CACHE_FRESHNESS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY,
                 "Freshness verify <when-expired | no-date | always | never> ", (char *) NULL);

  createArgument("when-expired", CMD_CONFIG_CACHE_FRESHNESS_VERIFY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY_WHEN_EXPIRED,
                 "Set freshness verify to be when-expired", (char *) NULL);

  createArgument("no-date", CMD_CONFIG_CACHE_FRESHNESS_VERIFY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NO_DATE,
                 "Set freshness verify to be no-date", (char *) NULL);

  createArgument("always", CMD_CONFIG_CACHE_FRESHNESS_VERIFY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY_ALWALYS,
                 "Set freshness verify to be always", (char *) NULL);

  createArgument("never", CMD_CONFIG_CACHE_FRESHNESS_VERIFY, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NEVER,
                 "Set the freshness verify to be never", (char *) NULL);

  createArgument("minimum", CMD_CONFIG_CACHE_FRESHNESS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_MINIMUM,
                 "Set freshness minimum <explicit | last-modified | nothing>", (char *) NULL);

  createArgument("explicit", CMD_CONFIG_CACHE_FRESHNESS_MINIMUM, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_EXPLICIT,
                 "Set the Freshness Minimum to be explicit", (char *) NULL);

  createArgument("last-modified", CMD_CONFIG_CACHE_FRESHNESS_MINIMUM, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_LAST_MODIFIED,
                 "Set the Freshness Minimum to be last modified", (char *) NULL);

  createArgument("nothing", CMD_CONFIG_CACHE_FRESHNESS_MINIMUM, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_NOTHING,
                 "Specify the Freshness minimum to be nothing", (char *) NULL);

  createArgument("no-expire-limit", CMD_CONFIG_CACHE_FRESHNESS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT,
                 "Set the Freshness no-expire-limit time", (char *) NULL);

  createArgument("greater-than", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_GREATER_THAN,
                 "Set the minimum Freshness no-expire-limit time", (char *) NULL);

  createArgument("less-than", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_FRESHNESS_NO_EXPIRE_LIMIT_LESS_THAN,
                 "Set the maximum Freshness no-expire-limit time", (char *) NULL);

  createArgument("dynamic", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_DYNAMIC, "Set Dynamic <on|off>", (char *) NULL);

  createArgument("alternates", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_ALTERNATES, "Set Alternates <on|off>", (char *) NULL);
  createArgument("vary", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_VARY, "Set vary <text | images | other> <field>", (char *) NULL);
  createArgument("text", CMD_CONFIG_CACHE_VARY, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_VARY_TEXT, "Set vary text's value", (char *) NULL);
  createArgument("images", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_VARY_COOKIES_IMAGES, "Set vary images' value", (char *) NULL);
  createArgument("other", CMD_CONFIG_CACHE_VARY, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_CACHE_VARY_OTHER, "Set vary other's value", (char *) NULL);

  createArgument("cookies", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES, "Set cookies <none | all | images | non-text | non-text-ext>",
                 (char *) NULL);
  createArgument("none", CMD_CONFIG_CACHE_COOKIES, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES_NONE, "No cookies", (char *) NULL);
  createArgument("all", CMD_CONFIG_CACHE_COOKIES, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES_ALL, "All cookies", (char *) NULL);
  createArgument("non-text", CMD_CONFIG_CACHE_COOKIES, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES_NON_TEXT, "Non-text cookies", (char *) NULL);
  createArgument("non-text-ext", CMD_CONFIG_CACHE_COOKIES, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_COOKIES_NON_TEXT_EXT, "Non-text-ext cookies", (char *) NULL);
  createArgument("clear", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_CACHE_CLEAR, "Clear the cache and start Traffic Server", (char *) NULL);
  return 0;
}




////////////////////////////////////////////////////////////////
// Cmd_ConfigHostdb
//
// This is the callback function for the "config:hostdb" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigHostdb(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:hostdb") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigHostdb argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_HOSTDB_LOOKUP_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.hostdb.lookup_timeout", argtable->arg_int));

    case CMD_CONFIG_HOSTDB_FOREGROUND_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.hostdb.timeout", argtable->arg_int));

    case CMD_CONFIG_HOSTDB_BACKGROUND_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.hostdb.verify_after", argtable->arg_int));

    case CMD_CONFIG_HOSTDB_INVALID_HOST_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.hostdb.fail.timeout", argtable->arg_int));

    case CMD_CONFIG_HOSTDB_RE_DNS_ON_RELOAD:
      return (Cli_RecordOnOff_Action(action, "proxy.config.hostdb.re_dns_on_reload", argtable->arg_string));
    case CMD_CONFIG_HOSTDB_CLEAR:
      return (ConfigHostdbClear());
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigHostdb
//
// Register "config:Hostdb" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigHostdb()
{
  createArgument("lookup-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_LOOKUP_TIMEOUT, "Lookup Timeout <seconds>", (char *) NULL);
  createArgument("foreground-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_FOREGROUND_TIMEOUT, "Foreground Timeout <minutes>", (char *) NULL);
  createArgument("background-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_BACKGROUND_TIMEOUT, "Background Timeout <minutes>", (char *) NULL);
  createArgument("invalid-host-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_INVALID_HOST_TIMEOUT, "Invalid Host Timeout <minutes>",
                 (char *) NULL);
  createArgument("re-dns-on-reload", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTDB_RE_DNS_ON_RELOAD, "Re-DNS on Reload Timeout <on|off>", (char *) NULL);
  createArgument("clear", 1, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_HOSTDB_CLEAR, "Clear the HostDB and start Traffic Server", (char *) NULL);
  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigDns
//
// This is the callback function for the "config:dns" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigDns(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:dns") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigDns argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_DNS_PROXY:
      return (Cli_RecordOnOff_Action(action, "proxy.config.dns.proxy.enabled", argtable->arg_string));

    case CMD_CONFIG_DNS_PROXY_PORT:
      return (Cli_RecordInt_Action(action, "proxy.config.dns.proxy_port", argtable->arg_int));

    case CMD_CONFIG_DNS_RESOLVE_TIMEOUT:
      return (Cli_RecordInt_Action(action, "proxy.config.dns.lookup_timeout", argtable->arg_int));

    case CMD_CONFIG_DNS_RETRIES:
      return (Cli_RecordInt_Action(action, "proxy.config.dns.retries", argtable->arg_int));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigDns
//
// Register "config:dns" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigDns()
{
  createArgument("proxy", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_DNS_PROXY, "Enable/disable DNS proxy feature <on | off>", (char *) NULL);
  createArgument("proxy-port", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_DNS_PROXY_PORT, "Specify DNS proxy port <int>", (char *) NULL);
  createArgument("resolve-timeout", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_DNS_RESOLVE_TIMEOUT, "Resolve timeout <int>", (char *) NULL);
  createArgument("retries", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_DNS_RETRIES, "Number of retries <int>", (char *) NULL);

  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigVirtualip
//
// This is the callback function for the "config:virtualip" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigVirtualip(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:virtualip") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigCache argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  int setvar = 0;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_VIRTUALIP_STATUS:
      return (Cli_RecordOnOff_Action((argc == 3), "proxy.config.vmap.enabled", argtable->arg_string));

    case CMD_CONFIG_VIRTUALIP_LIST:
      return (ConfigVirtualIpList());

    case CMD_CONFIG_VIRTUALIP_ADD:
      if (argc == 8) {
        setvar = 1;
      }
      Cli_PrintArg(0, argtable);
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      if (ConfigVirtualipAdd(argtable[1].arg_string, argtable[2].arg_string, argtable[3].arg_int, setvar) == CLI_OK) {
        return CMD_OK;
      } else {
        return CMD_ERROR;
      }

    case CMD_CONFIG_VIRTUALIP_DELETE:
      if (argc == 3) {
        setvar = 1;
      }
      if (ConfigVirtualipDelete(argtable[0].arg_int, setvar) == CLI_OK) {
        return CMD_OK;
      } else {
        return CMD_ERROR;
      }
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigVirtualip
//
// Register "config:virtualip" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigVirtualip()
{

  createArgument("status", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_STATUS, "Virtual IP <on | off>", (char *) NULL);
  createArgument("list", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_LIST, "List virtual IP addresses", (char *) NULL);
  createArgument("add", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_ADD, "add ip <x.x.x.x> device <string> sub-intf <int>",
                 (char *) NULL);
  createArgument("ip", CMD_CONFIG_VIRTUALIP_ADD, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_ADD_IP, "Virtual IP Address <x.x.x.x>", (char *) NULL);
  createArgument("device", CMD_CONFIG_VIRTUALIP_ADD_IP, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_ADD_DEVICE, "Virtual IP device <string>", (char *) NULL);
  createArgument("sub-intf", CMD_CONFIG_VIRTUALIP_ADD_DEVICE, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_ADD_SUBINTERFACE, "Virtual IP sub interface <integer>",
                 (char *) NULL);
  createArgument("delete", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_VIRTUALIP_DELETE, "Delete Virtual IP <integer>", (char *) NULL);
  return CLI_OK;
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigLogging
//
// This is the callback function for the "config:logging" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigLogging(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:logging") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigCache argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  int setvar = 0;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_LOGGING_EVENT:
      if (argc == 3) {
        setvar = 1;
      }
      return (ConfigLoggingEvent(argtable[1].parsed_args, setvar));
    case CMD_CONFIG_LOGGING_MGMT_DIRECTORY:
      return (Cli_RecordString_Action((argc == 3), "proxy.config.log2.logfile_dir", argtable->arg_string));

    case CMD_CONFIG_LOGGING_SPACE_LIMIT:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.log2.max_space_mb_for_logs", argtable->arg_int));

    case CMD_CONFIG_LOGGING_SPACE_HEADROOM:
      return (Cli_RecordInt_Action((argc == 3), "proxy.config.log2.max_space_mb_headroom", argtable->arg_int));

    case CMD_CONFIG_LOGGING_COLLATION_STATUS:
      if (argc == 3) {
        setvar = 1;
      }
      return (ConfigLoggingCollationStatus(argtable[1].parsed_args, setvar));
    case CMD_CONFIG_LOGGING_COLLATION_HOST:
      return (Cli_RecordString_Action((argc == 3), "proxy.config.log2.collation_host", argtable->arg_string));

    case CMD_CONFIG_LOGGING_COLLATION:
      if (argc == 8) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      Cli_PrintArg(4, argtable);
      return (ConfigLoggingCollation(argtable[1].arg_string, argtable[3].parsed_args, argtable[4].arg_int, setvar));
    case CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT:
      if (argc == 10) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      Cli_PrintArg(4, argtable);
      Cli_PrintArg(5, argtable);
      Cli_PrintArg(6, argtable);
      return (ConfigLoggingFormatTypeFile(argtable[1].parsed_args,
                                          argtable[2].parsed_args,
                                          argtable[4].parsed_args,
                                          argtable[5].arg_string, argtable[6].arg_string, setvar));
    case CMD_CONFIG_LOGGING_SPLITTING:
      if (argc == 4) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      return (ConfigLoggingSplitting(argtable[1].parsed_args, argtable[2].parsed_args, setvar));

    case CMD_CONFIG_LOGGING_CUSTOM:
      if (argc == 5) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      return (ConfigLoggingCustomFormat(argtable[1].parsed_args, argtable[3].parsed_args, setvar));

    case CMD_CONFIG_LOGGING_ROLLING:
      if (argc == 9) {
        setvar = 1;
      }
      Cli_PrintArg(1, argtable);
      Cli_PrintArg(2, argtable);
      Cli_PrintArg(3, argtable);
      Cli_PrintArg(4, argtable);
      Cli_PrintArg(5, argtable);
      return (ConfigLoggingRollingOffsetIntervalAutodelete(argtable[1].parsed_args,
                                                           argtable[2].arg_int,
                                                           argtable[3].arg_int, argtable[5].parsed_args, setvar));

    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigLogging
//
// Register "config:logging" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigLogging()
{

  createArgument("on", CLI_ARGV_NO_POS, CLI_ARGV_REQUIRED,
                 (char *) NULL, CMD_CONFIG_LOGGING_ON, "Enable logging", (char *) NULL);
  createArgument("off", CLI_ARGV_NO_POS, CLI_ARGV_REQUIRED,
                 (char *) NULL, CMD_CONFIG_LOGGING_OFF, "Disable logging", (char *) NULL);
  createArgument("event", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT, "Events <enabled | trans-only | error-only | disabled>",
                 (char *) NULL);
  createArgument("enabled", CMD_CONFIG_LOGGING_EVENT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT_ENABLED, "Event logging enabled", (char *) NULL);
  createArgument("trans-only", CMD_CONFIG_LOGGING_EVENT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT_TRANS_ONLY, "Event logging for transactions only",
                 (char *) NULL);
  createArgument("error-only", CMD_CONFIG_LOGGING_EVENT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT_ERROR_ONLY, "Event logging for errors only", (char *) NULL);
  createArgument("disabled", CMD_CONFIG_LOGGING_EVENT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_EVENT_DISABLED, "Event logging is disabled", (char *) NULL);
  createArgument("mgmt-directory", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_LOGGING_MGMT_DIRECTORY, "Logging MGMT directory <string>", (char *) NULL);
  createArgument("space-limit", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPACE_LIMIT, "Space limit for logs <mb>", (char *) NULL);
  createArgument("space-headroom", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPACE_HEADROOM, "Space for headroom <mb>", (char *) NULL);
  createArgument("collation-status", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_STATUS,
                 "Collation status <inactive | host | send-standard |\n" "                   send-custom | send-all>",
                 (char *) NULL);
  createArgument("inactive", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_CONSTANT, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_INACTIVE, "No collation", (char *) NULL);
  createArgument("host", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_CONSTANT, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_HOST, "Be a collation host (receiver)", (char *) NULL);
  createArgument("send-standard", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_STANDARD, "Send standard logs", (char *) NULL);
  createArgument("send-custom", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_CUSTOM, "Send custom logs", (char *) NULL);
  createArgument("send-all", CMD_CONFIG_LOGGING_COLLATION_STATUS, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL,
                 CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_ALL, "Send all logs", (char *) NULL);
  createArgument("collation-host", 1, CLI_ARGV_OPTION_NAME_VALUE, (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_HOST,
                 "Specify the collation host <string>", (char *) NULL);

  createArgument("collation", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION, "Collation parameters secret <secret> tagged <on | off>\n"
                 "                   orphan-limit <orphan>", (char *) NULL);

  createArgument("secret", CMD_CONFIG_LOGGING_COLLATION, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_SECRET, "Collation secret is <string>", (char *) NULL);

  createArgument("tagged", CLI_ARGV_NO_POS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_TAGGED, "Collation tagged is <on | off>", (char *) NULL);

  createArgument("orphan-limit", CLI_ARGV_NO_POS, CLI_ARGV_INT,
                 (char *) NULL, CMD_CONFIG_LOGGING_COLLATION_ORPHAN_LIMIT,
                 "Collation orphan limit size <mb>", (char *) NULL);

  createArgument("format", CLI_ARGV_NO_POS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT,
                 "Logging format <squid | netscape-common | netscape-ext |\n"
                 "                   netscape-ext2>", (char *) NULL);

  createArgument("squid", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_FORMAT_SQUID, "Squid <on | off>", (char *) NULL);
  createArgument("netscape-common", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_COMMON, "Netscape Common <on | off>", (char *) NULL);
  createArgument("netscape-ext", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT, "Netscape Extended <on | off>", (char *) NULL);
  createArgument("netscape-ext2", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT2,
                 "Netscape Extended 2 <on | off>", (char *) NULL);

  createArgument("type", CLI_ARGV_NO_POS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_TYPE, "Logging type <ascii | binary>", (char *) NULL);
  createArgument("ascii", CMD_CONFIG_LOGGING_TYPE, CLI_ARGV_REQUIRED,
                 (char *) NULL, CMD_CONFIG_LOGGING_TYPE_ASCII, "ASCII log files", (char *) NULL);
  createArgument("binary", CMD_CONFIG_LOGGING_TYPE, CLI_ARGV_REQUIRED,
                 (char *) NULL, CMD_CONFIG_LOGGING_TYPE_BINARY, "Binary log files", (char *) NULL);

  createArgument("file", CLI_ARGV_NO_POS, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_LOGGING_FILE, "Log file name <string>", (char *) NULL);

  createArgument("header", CLI_ARGV_NO_POS, CLI_ARGV_STRING,
                 (char *) NULL, CMD_CONFIG_LOGGING_HEADER, "Log file header <string>", (char *) NULL);

  createArgument("splitting", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPLITTING,
                 "Splitting of logs for protocols <nntp | icp | http>", (char *) NULL);
  createArgument("nntp", CMD_CONFIG_LOGGING_SPLITTING, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPLITTING_NNTP, "Split NNTP <on | off>", (char *) NULL);
  createArgument("icp", CMD_CONFIG_LOGGING_SPLITTING, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPLITTING_ICP, "Split ICP <on | off>", (char *) NULL);
  createArgument("http", CMD_CONFIG_LOGGING_SPLITTING, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_SPLITTING_HTTP, "Split of HTTP <on | off>", (char *) NULL);

  createArgument("custom", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_CUSTOM,
                 "Custom Logging <on | off> format <traditional | xml>", (char *) NULL);

  createArgument("traditional", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_CUSTOM_FORMAT_TRADITIONAL,
                 "Custom logging in traditional format", (char *) NULL);

  createArgument("xml", CMD_CONFIG_LOGGING_AND_CUSTOM_FORMAT, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_LOGGING_CUSTOM_FORMAT_XML, "Custom logging in XML", (char *) NULL);

  createArgument("rolling", 1, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_ROLLING,
                 "Log file rolling <on | off> offset <hour>\n"
                 "                   interval <num-hours> auto-delete <on | off>", (char *) NULL);

  createArgument("offset", CLI_ARGV_NO_POS, CLI_ARGV_INT,
                 (char *) NULL, CMD_CONFIG_LOGGING_OFFSET, "Rolling offset <hour> (24hour format)", (char *) NULL);

  createArgument("interval", CLI_ARGV_NO_POS, CLI_ARGV_INT,
                 (char *) NULL, CMD_CONFIG_LOGGING_INTERVAL, "Rolling interval <seconds>", (char *) NULL);
  createArgument("auto-delete", CLI_ARGV_NO_POS, CLI_ARGV_CONST_OPTION,
                 (char *) NULL, CMD_CONFIG_LOGGING_AUTO_DELETE, "Auto delete <on | off>", (char *) NULL);
  return 0;


}

////////////////////////////////////////////////////////////////
// Cmd_ConfigSsl
//
// This is the callback function for the "config:ssl" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigSsl(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{

  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:ssl") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigSsl argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_SSL_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.ssl.enabled", argtable->arg_string));

    case CMD_CONFIG_SSL_PORT:
      return (Cli_RecordInt_Action(action, "proxy.config.ssl.server_port", argtable->arg_int));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}



////////////////////////////////////////////////////////////////
// CmdArgs_ConfigSsl
//
// Register "config:ssl" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigSsl()
{
  createArgument("status", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_SSL_STATUS, "SSL <on | off>", (char *) NULL);

  createArgument("ports", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_SSL_PORT, "SSL port <int>", (char *) NULL);
  return 0;
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigAlarm
//
// This is the callback function for the "config:alarm" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigAlarm(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function 
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:alarm") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigAlarm argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable, *infoPtr;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;
  infoPtr = argtable;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_ALARM_RESOLVE_NAME:
      if (argc < 3) {
        return (ShowAlarms());
      }
      return (ConfigAlarmResolveName(argtable->arg_string));
    case CMD_CONFIG_ALARM_RESOLVE_NUMBER:
      if (argc < 3) {
        return (ShowAlarms());
      }
      return (ConfigAlarmResolveNumber(argtable->arg_int));
    case CMD_CONFIG_ALARM_RESOLVE_ALL:
      return (ConfigAlarmResolveAll());
    case CMD_CONFIG_ALARM_NOTIFY:
      Cli_Debug("Cmd_ConfigAlarm \"%s\"\n", argtable->arg_string);
      return (ConfigAlarmNotify(argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}



////////////////////////////////////////////////////////////////
// CmdArgs_ConfigAlarm
//
// Register "config:alarm" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigAlarm()
{
  createArgument("resolve-name", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_ALARM_RESOLVE_NAME, "Resolve by name <string>", (char *) NULL);

  createArgument("resolve-number", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_ALARM_RESOLVE_NUMBER, "Resolve by number from list <int>", (char *) NULL);

  createArgument("resolve-all", CLI_ARGV_NO_POS, CLI_ARGV_CONSTANT,
                 (char *) NULL, CMD_CONFIG_ALARM_RESOLVE_ALL, "Resolve all alarms", (char *) NULL);

  createArgument("notify", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_ALARM_NOTIFY, "Alarm notification <on | off>", (char *) NULL);
  return CLI_OK;
}


////////////////////////////////////////////////////////////////
// Cmd_ConfigNtlm
//
int
Cmd_ConfigNtlm(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:ntlm") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigNtlm argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_NTLM_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.ntlm.auth.enabled", argtable->arg_string));

    case CMD_CONFIG_NTLM_DOMAIN_CTRL:
      return (Cli_RecordString_Action(action, "proxy.config.ntlm.dc.list", argtable->arg_string));

    case CMD_CONFIG_NTLM_NTDOMAIN:
      return (Cli_RecordString_Action(action, "proxy.config.ntlm.nt_domain", argtable->arg_string));

    case CMD_CONFIG_NTLM_LOADBAL:
      return (Cli_RecordOnOff_Action(action, "proxy.config.ntlm.dc.load_balance", argtable->arg_string));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigNtlm
//
// Register "config:Scheduled-Update" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigNtlm()
{
  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NTLM_STATUS,
                 "Enable or disable NTLM authentication <on | off>", (char *) NULL);

  createArgument("domain-controller", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NTLM_DOMAIN_CTRL,
                 "Comma separated list of hostnames of domain controllers <string>", (char *) NULL);

  createArgument("nt-domain", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NTLM_NTDOMAIN,
                 "NT domain name that should be authenticated against <string>", (char *) NULL);

  createArgument("load-balancing", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NTLM_LOADBAL,
                 "Balance the load on sending requests to domain controllers <on | off>", (char *) NULL);

  return 0;
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigRadius
//
int
Cmd_ConfigRadius(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:radius") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigRadius argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;

  int action = (argc == 3) ? RECORD_SET : RECORD_GET;

  if (argtable->parsed_args != CLI_PARSED_ARGV_END) {
    switch (argtable->parsed_args) {
    case CMD_CONFIG_RADIUS_STATUS:
      return (Cli_RecordOnOff_Action(action, "proxy.config.radius.auth.enabled", argtable->arg_string));

    case CMD_CONFIG_RADIUS_PRI_HOST:
      return (Cli_RecordString_Action(action,
                                      "proxy.config.radius.proc.radius.primary_server.name", argtable->arg_string));

    case CMD_CONFIG_RADIUS_PRI_PORT:
      return (Cli_RecordInt_Action(action,
                                   "proxy.config.radius.proc.radius.primary_server.auth_port", argtable->arg_int));

    case CMD_CONFIG_RADIUS_SEC_HOST:
      return (Cli_RecordString_Action(action,
                                      "proxy.config.radius.proc.radius.secondary_server.name", argtable->arg_string));
    case CMD_CONFIG_RADIUS_SEC_PORT:
      return (Cli_RecordInt_Action(action,
                                   "proxy.config.radius.proc.radius.secondary_server.auth_port", argtable->arg_int));

    case CMD_CONFIG_RADIUS_PRI_KEY:
      return (ConfigRadiusKeys("proxy.config.radius.proc.radius.primary_server.shared_key_file"));

    case CMD_CONFIG_RADIUS_SEC_KEY:
      return (ConfigRadiusKeys("proxy.config.radius.proc.radius.secondary_server.shared_key_file"));
    }
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigRadius
//
// Register "config:radius" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigRadius()
{
  createArgument("status", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_RADIUS_STATUS,
                 "Enable or disable Radius authentication <on | off>", (char *) NULL);

  createArgument("primary-hostname", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_RADIUS_PRI_HOST,
                 "Specify the name of the primary Radius server <string>", (char *) NULL);

  createArgument("primary-port", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_RADIUS_PRI_PORT,
                 "Specify the authentication port of the primary Radius server <integer>", (char *) NULL);

  createArgument("primary-key", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_RADIUS_PRI_KEY, "Specify the key to use for encoding", (char *) NULL);

  createArgument("secondary-hostname", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_RADIUS_SEC_HOST,
                 "Specify the name of the secondary Radius server <string>", (char *) NULL);

  createArgument("secondary-port", 1, CLI_ARGV_OPTION_INT_VALUE,
                 (char *) NULL, CMD_CONFIG_RADIUS_SEC_PORT,
                 "Specify the authentication port of the secondary Radius server <integer>", (char *) NULL);

  createArgument("secondary-key", 1, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_RADIUS_SEC_KEY, "Specify the key to use for encoding", (char *) NULL);

  return 0;
}



////////////////////////////////////////////////////////////////
//
// "config" sub-command implementations
//
////////////////////////////////////////////////////////////////


// config start sub-command
int
ConfigStart()
{
  INKProxyStateT state = INKProxyStateGet();

  switch (state) {
  case INK_PROXY_ON:
    // do nothing, proxy is already on
    Cli_Error(ERR_PROXY_STATE_ALREADY, "on");
    break;
  case INK_PROXY_OFF:
  case INK_PROXY_UNDEFINED:
    if (INKProxyStateSet(INK_PROXY_ON, INK_CACHE_CLEAR_OFF)) {
      Cli_Error(ERR_PROXY_STATE_SET, "on");
      return CLI_ERROR;
    }
    break;
  }
  return CLI_OK;
}

// config stop sub-command
int
ConfigStop()
{
  INKProxyStateT state = INKProxyStateGet();

  switch (state) {
  case INK_PROXY_OFF:
    // do nothing, proxy is already off
    Cli_Error(ERR_PROXY_STATE_ALREADY, "off");
    break;
  case INK_PROXY_ON:
  case INK_PROXY_UNDEFINED:
    if (INKProxyStateSet(INK_PROXY_OFF, INK_CACHE_CLEAR_OFF)) {
      Cli_Error(ERR_PROXY_STATE_SET, "off");
      return CLI_ERROR;
    }
    break;
  }
  return CLI_OK;
}

// config get sub-command
//   used to get the value of any config variable in records.config
int
ConfigGet(const char *rec_name)
{
  Cli_Debug("ConfigGet: rec_name %s\n", rec_name);

  INKError status;
  INKRecordEle rec_val;

  status = Cli_RecordGet(rec_name, &rec_val);

  if (status) {
    return status;
  }
  // display the result

  switch (rec_val.rec_type) {
  case INK_REC_INT:            // INK64 aka "long long"
    Cli_Printf("%s = %d\n", rec_name, (int) rec_val.int_val);
    break;
  case INK_REC_COUNTER:        // INK64 aka "long long"
    Cli_Printf("%s = %d\n", rec_name, (int) rec_val.counter_val);
    break;
  case INK_REC_FLOAT:          // float
    Cli_Printf("%s = %f\n", rec_name, rec_val.float_val);
    break;
  case INK_REC_STRING:         // char*
    Cli_Printf("%s = \"%s\"\n", rec_name, rec_val.string_val);
    break;
  case INK_REC_LLONG:          // INK64 aka "long long"
    Cli_Printf("%s = %lld\n", rec_name, rec_val.llong_val);
    break;
  case INK_REC_UNDEFINED:      // what's this???
    Cli_Printf("%s = UNDEFINED\n", rec_name);
    break;
  }

  return CLI_OK;
}

// config set sub-command
//   used to set the value of any variable in records.config

int
ConfigSet(const char *rec_name, const char *value)
{
  Cli_Debug("ConfigSet: rec_name %s value %s\n", rec_name, value);

  INKError status;
  INKActionNeedT action_need;

  status = Cli_RecordSet(rec_name, value, &action_need);
  if (status) {
    return status;
  }

  return (Cli_ConfigEnactChanges(action_need));
}

// config name sub-command
//   used to set or display the value of proxy.config.proxy_name

int
ConfigName(const char *proxy_name)
{
  INKError status = INK_ERR_OKAY;
  INKString str_val = NULL;
  INKActionNeedT action_need;

  if (proxy_name) {             // set the name

    Cli_Debug("ConfigName: set name proxy_name %s\n", proxy_name);

    status = Cli_RecordSetString("proxy.config.proxy_name", (INKString) proxy_name, &action_need);

    if (status) {
      return status;
    }

    return (Cli_ConfigEnactChanges(action_need));

  }

  else {                        // display the name
    Cli_Debug("ConfigName: get name\n");

    status = Cli_RecordGetString("proxy.config.proxy_name", &str_val);
    if (status) {
      return status;
    }

    if (str_val) {
      Cli_Printf("%s\n", str_val);
    } else {
      Cli_Printf("none\n");
    }
  }

  return CLI_OK;

}

// config ports sub-command
//   used to set the value of port(s)

int
ConfigPortsSet(int arg_ref, void *valuePtr)
{

  switch (arg_ref) {
  case CMD_CONFIG_PORTS_HTTP_OTHER:
  case CMD_CONFIG_PORTS_SSL:
    Cli_Debug("ConfigPortsSet: arg_ref %d value %s\n", arg_ref, (char *) valuePtr);
    break;
  default:
    Cli_Debug("ConfigPortsSet: arg_ref %d value %d\n", arg_ref, *(INKInt *) valuePtr);
  }

  INKError status = INK_ERR_OKAY;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;

  Cli_Debug("ConfigPorts: set\n");
  switch (arg_ref) {
  case CMD_CONFIG_PORTS_HTTP_SERVER:
    status = Cli_RecordSetInt("proxy.config.http.server_port", *(INKInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_HTTP_OTHER:
    status = Cli_RecordSetString("proxy.config.http.server_other_ports", (INKString) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_WEBUI:
    status = Cli_RecordSetInt("proxy.config.admin.web_interface_port", *(INKInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_OVERSEER:
    status = Cli_RecordSetInt("proxy.config.admin.overseer_port", *(INKInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_CLUSTER:
    status = Cli_RecordSetInt("proxy.config.cluster.cluster_port", *(INKInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_CLUSTER_RS:
    status = Cli_RecordSetInt("proxy.config.cluster.rsport", *(INKInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_CLUSTER_MC:
    status = Cli_RecordSetInt("proxy.config.cluster.mcport", *(INKInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_NNTP_SERVER:
    status = Cli_RecordSetInt("proxy.config.nntp.server_port", *(INKInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_FTP_SERVER:
    status = Cli_RecordSetInt("proxy.config.ftp.proxy_server_port", *(INKInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_SSL:
    status = Cli_RecordSetString("proxy.config.http.ssl_ports", (INKString) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_SOCKS_SERVER:
    status = Cli_RecordSetInt("proxy.config.socks.socks_server_port", *(INKInt *) valuePtr, &action_need);
    break;
  case CMD_CONFIG_PORTS_ICP:
    status = Cli_RecordSetInt("proxy.config.icp.icp_port", *(INKInt *) valuePtr, &action_need);
    break;
  }

  if (status) {
    return status;
  }

  return (Cli_ConfigEnactChanges(action_need));

}

// config ports sub-command
//   used to display the value of port(s)

int
ConfigPortsGet(int arg_ref)
{
  INKError status = INK_ERR_OKAY;
  INKInt int_val = -1;
  INKString str_val = NULL;

  Cli_Debug("ConfigPortsGet: get\n");

  switch (arg_ref) {
  case CMD_CONFIG_PORTS_HTTP_SERVER:
    status = Cli_RecordGetInt("proxy.config.http.server_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_HTTP_OTHER:
    status = Cli_RecordGetString("proxy.config.http.server_other_ports", &str_val);
    if (status) {
      return status;
    }
    if (str_val) {
      Cli_Printf("%s\n", str_val);
    } else {
      Cli_Printf("none\n");
    }
    break;
  case CMD_CONFIG_PORTS_WEBUI:
    status = Cli_RecordGetInt("proxy.config.admin.web_interface_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_OVERSEER:
    status = Cli_RecordGetInt("proxy.config.admin.overseer_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_CLUSTER:
    status = Cli_RecordGetInt("proxy.config.cluster.cluster_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_CLUSTER_RS:
    status = Cli_RecordGetInt("proxy.config.cluster.rsport", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_CLUSTER_MC:
    status = Cli_RecordGetInt("proxy.config.cluster.mcport", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_NNTP_SERVER:
    status = Cli_RecordGetInt("proxy.config.nntp.server_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_FTP_SERVER:
    status = Cli_RecordGetInt("proxy.config.ftp.proxy_server_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_SSL:
    status = Cli_RecordGetString("proxy.config.http.ssl_ports", &str_val);
    if (status) {
      return status;
    }
    if (str_val) {
      Cli_Printf("%s\n", str_val);
    } else {
      Cli_Printf("none\n");
    }
    break;
  case CMD_CONFIG_PORTS_SOCKS_SERVER:
    status = Cli_RecordGetInt("proxy.config.socks.socks_server_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  case CMD_CONFIG_PORTS_ICP:
    status = Cli_RecordGetInt("proxy.config.icp.icp_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    break;
  default:
    Cli_Error(ERR_COMMAND_SYNTAX,
              "\n\nconfig:ports <http-server | http-other | webui | \n overseer | cluster-rs | cluster-mc | \n nntp-server | ftp-server | ssl | \n socks-server | icp > \n <port | ports list>\n");

    return CLI_ERROR;
  }
  return CLI_OK;
}

// config filter sub-command
int
ConfigFilter(const char *url)
{
  Cli_Debug("ConfigFilter: url %s\n", url);

  return (Cli_ConfigFileURL_Action(INK_FNAME_MGMT_ALLOW, "filter.config", url));
  //   return (Cli_SetConfigFileFromUrl(INK_FNAME_FILTER, url));
}

int
ConfigSecurityPasswd()
{
  char org_passwd[256], new_passwd1[256], new_passwd2[256], ch = 'p';
  int i = 0;
  char *e_passwd = NULL;
  INKError status = INK_ERR_OKAY;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;

  Cli_Debug("ConfigSecurityPasswd\n");

  Cli_Printf("Enter Old Password:");
  fflush(stdout);
  ch = u_getch();
  while (ch != '\n' && ch != '\r') {
    org_passwd[i] = ch;
    i++;
    ch = u_getch();

  }
  org_passwd[i] = 0;

  if (cliVerifyPasswd(org_passwd) == CLI_ERROR) {
    Cli_Printf("\nIncorrect Password\n");
    return CLI_ERROR;
  }

  Cli_Printf("\nEnter New Password:");
  fflush(stdout);
  i = 0;
  ch = u_getch();
  while (ch != '\n' && ch != '\r') {
    new_passwd1[i] = ch;
    i++;
    ch = u_getch();

  }
  new_passwd1[i] = 0;

  Cli_Printf("\nReEnter New Password:");
  fflush(stdout);
  i = 0;
  ch = u_getch();
  while (ch != '\n' && ch != '\r') {
    new_passwd2[i] = ch;
    i++;
    ch = u_getch();

  }
  new_passwd2[i] = 0;

  if (strcmp(new_passwd1, new_passwd2)) {
    Cli_Printf("\nTwo New Passwords Aren't Same\n");
    return CLI_ERROR;
  }

  INKEncryptPassword(new_passwd1, &e_passwd);
  status = Cli_RecordSetString("proxy.config.admin.admin_password", (INKString) e_passwd, &action_need);

  if (status != INK_ERR_OKAY) {
    Cli_Printf("\nCannot Set The Password\n");
    Cli_ConfigEnactChanges(action_need);
    if (e_passwd)
      xfree(e_passwd);
    return CLI_ERROR;
  }

  Cli_Printf("\nPassword Set\n");
  if (e_passwd)
    xfree(e_passwd);
  return CLI_OK;

}

// config remap sub-command
int
ConfigRemap(const char *url)
{
  Cli_Debug("ConfigRemap: url %s\n", url);

  return (Cli_SetConfigFileFromUrl(INK_FNAME_REMAP, url));
}

// config date sub-command
//   used to set or display the system date

int
ConfigDate(char *datestr)
{

  if (datestr) {                // set the date
    Cli_Debug("ConfigDate: set date %s\n", datestr);

    if (getuid() != 0) {
      Cli_Printf("\nMust be \"root\" user to change the date.\n"
                 "Use \"config:root\" command to switch to root user.\n");
      return CLI_ERROR;
    }


    struct tm *mPtr;
    struct timeval v;
    struct DateTime t;
    int yy, mm, dd;

    memset(&v, 0, sizeof(struct timeval));
    memset(&t, 0, sizeof(struct DateTime));


    // add strlen() check to avoid string overflow, so we can disable coverity check here
    // coverity[secure_coding]
    if (strlen(datestr) != 10 || sscanf(datestr, "%[0-9]/%[0-9]/%[0-9]", t.str_mm, t.str_dd, t.str_yy) != 3) {
      Cli_Printf("Error: <date> = mm/dd/yyyy \n");
      return CLI_ERROR;
    }

    Cli_Debug("%s-%s-%s\n", t.str_mm, t.str_dd, t.str_yy);

    mm = atoi(t.str_mm);
    dd = atoi(t.str_dd);
    yy = atoi(t.str_yy);

    Cli_Debug("%d-%d-%d\n", mm, dd, yy);

    if (!((dd >= 1) && (dd <= 31)
          && (mm >= 1) && (mm <= 12)
          && (yy >= 1900) && (yy <= 2100))) {
      Cli_Printf("Error: Invalid Date Value \n");
      return CLI_ERROR;
    }

    if (gettimeofday(&v, NULL)) {
      Cli_Debug("Error Getting Time \n");
      return CLI_ERROR;
    }

    mPtr = localtime(&(v.tv_sec));

    mPtr->tm_mday = dd;
    mPtr->tm_mon = mm - 1;
    mPtr->tm_year = yy - 1900;
    if ((v.tv_sec = mktime(mPtr)) < 0) {
      Cli_Printf("ERROR: invalid date \n");
      return CLI_ERROR;
    }

    Cli_Printf("Stopping Proxy software while changing clock settings.\n");

    StopTrafficServer();

    if (settimeofday(&v, NULL) == -1) {
      Cli_Printf("Error: could not update date \n");
      StartTrafficServer();
      return CLI_ERROR;
    }
    if (system("/sbin/hwclock --systohc --utc") != 0) {
      Cli_Error("ERROR: Unable to set hardware clock.\n");
      exit(1);
    }
    StartTrafficServer();
  }


  Cli_Debug("Config:clock: get date\n");
  if (system("date '+DATE: %m/%d/%Y'") == (-1))
    return CLI_ERROR;

  return CLI_OK;

}


// config time sub-command
//   used to set or display the system time
int
ConfigTime(char *timestr)
{

  if (timestr) {                // set the time
    Cli_Debug("ConfigTime: set time %s\n", timestr);
    if (getuid() != 0) {
      Cli_Printf("\nMust be \"root\" user to change the time.\n"
                 "Use \"config:root\" command to switch to root user.\n");
      return CLI_ERROR;
    }

    struct tm *mPtr;
    struct timeval v;
    struct DateTime t;
    int hour, min, sec;

    memset(&v, 0, sizeof(struct timeval));
    memset(&t, 0, sizeof(struct DateTime));

    // add strlen() check to avoid string overflow, so we can disable coverity check here
    // coverity[secure_coding]
    if (strlen(timestr) != 8 || sscanf(timestr, "%[0-9]:%[0-9]:%[0-9]", t.str_hh, t.str_min, t.str_ss) != 3) {
      Cli_Printf("Error: <time> = hh:mm:ss \n");
      return CLI_ERROR;
    }

    Cli_Debug("%s-%s-%s\n", t.str_hh, t.str_min, t.str_ss);

    hour = atoi(t.str_hh);
    min = atoi(t.str_min);
    sec = atoi(t.str_ss);
    Cli_Debug("%d-%d-%d\n", hour, min, sec);

    if (!((hour >= 0) && (hour <= 23)
          && (min >= 0) && (min <= 59)
          && (sec >= 0) && (sec <= 59))) {
      Cli_Printf("ERROR: Invalid Time Value \n");
      return CLI_ERROR;
    }

    if (gettimeofday(&v, NULL)) {
      Cli_Printf("Error Getting Time \n");
      return CLI_ERROR;
    }

    mPtr = localtime(&(v.tv_sec));

    mPtr->tm_sec = sec;
    mPtr->tm_min = min;
    mPtr->tm_hour = hour;
    if ((v.tv_sec = mktime(mPtr)) < 0) {
      Cli_Printf("ERROR: invalid time \n");
      return CLI_ERROR;
    }

    Cli_Printf("Stopping Proxy software while changing clock settings.\n");

    StopTrafficServer();

    if (settimeofday(&v, NULL) == -1) {
      Cli_Printf("ERROR: could not update time \n");

      StartTrafficServer();

      return CLI_ERROR;
    }

    if (system("/sbin/hwclock --systohc --utc") != 0) {
      Cli_Error("ERROR: Unable to set hardware clock.\n");
    }

    StartTrafficServer();
  }

  Cli_Debug("Config:clock: get time\n");
  if (system("date '+TIME: %H:%M:%S'") == (-1))
    Cli_Error("ERROR: Unable to set date.\n");

  return CLI_OK;
}

// config timezone sub-command
//   used to set the system timezone
int
ConfigTimezone(int index, int setvar)
{

  Cli_Debug("ConfigTime: %d %d\n", index, setvar);

  FILE *fp, *tmp;
  const char *zonetable = "/usr/share/zoneinfo/zone.tab";
  char command[256];
  char buffer[1024];
  char old_zone[1024];
  char new_zone[1024];
  char *zone;

  ink_strncpy(new_zone, "", sizeof(new_zone));

  fp = fopen(zonetable, "r");
  tmp = fopen("/tmp/zonetab.tmp", "w");
  if (fp == NULL || tmp == NULL) {
    printf("can not open the file\n");
    return CLI_ERROR;
  }
  NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  while (!feof(fp)) {
    if (buffer[0] != '#') {
      strtok(buffer, " \t");
      strtok(NULL, " \t");
      zone = strtok(NULL, " \t");
      if (zone[strlen(zone) - 1] == '\n') {
        zone[strlen(zone) - 1] = '\0';
      }
      fprintf(tmp, "%s\n", zone);
    }
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  }
  fclose(fp);
  fclose(tmp);
  remove("/tmp/zonetab");
  NOWARN_UNUSED_RETURN(system("/bin/sort /tmp/zonetab.tmp > /tmp/zonetab"));

  fp = fopen("/tmp/zonetab", "r");
  NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  int i = 0;
  while (!feof(fp)) {
    zone = buffer;
    if (zone[strlen(zone) - 1] == '\n') {
      zone[strlen(zone) - 1] = '\0';
    }
    if (setvar) {
      if (index == i) {
        ink_strncpy(new_zone, zone, sizeof(new_zone));
      }
    }
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
    i++;
  }
  fclose(fp);
  remove("/tmp/zonetab.tmp");
  remove("/tmp/zonetab");

  switch (setvar) {
  case 0:                      //get
    find_value("/etc/sysconfig/clock", "ZONE", old_zone, sizeof(old_zone), "=", 0);
    if (strlen(old_zone)) {
      Cli_Printf("%s\n", old_zone);
    } else {
      Cli_Printf("NULL\n");
    }
    return CLI_OK;

  case 1:                      //set
    if (getuid() != 0) {
      Cli_Printf("\nMust be \"root\" user to change the timezone.\n"
                 "Use \"config:root\" command to switch to root user.\n");
      return CLI_ERROR;
    }

    if (!strlen(new_zone)) {
      Cli_Error("ERROR: Invalid timezone specified.\n");
      return CLI_ERROR;
    }

    Cli_Printf("Stopping Proxy software while changing clock settings.\n");

    StopTrafficServer();

    Cli_Printf("New timezone is %s\n", new_zone);

    fp = fopen("/etc/sysconfig/clock", "r");
    tmp = fopen("/tmp/clock.tmp", "w");
    NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
    while (!feof(fp)) {
      if (strstr(buffer, "ZONE") != NULL) {
        fprintf(tmp, "ZONE=\"%s\"\n", new_zone);
      } else if (strstr(buffer, "UTC") != NULL) {
        fprintf(tmp, "UTC=true\n");
      } else {
        fputs(buffer, tmp);
      }
      NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
    }
    fclose(fp);
    fclose(tmp);
    if (system("/bin/mv /tmp/clock.tmp /etc/sysconfig/clock") == (-1))
      return CLI_ERROR;

    ink_snprintf(command, sizeof(command), "/bin/cp -f /usr/share/zoneinfo/%s /etc/localtime", new_zone);
    if (system(command) == (-1))
      return CLI_ERROR;

    StartTrafficServer();

    return CLI_OK;

  }
  Cli_Printf("Error in File Open to Read\n");
  return CLI_ERROR;
}

int
ConfigTimezoneList()
{
  FILE *fp, *tmp;
  const char *zonetable = "/usr/share/zoneinfo/zone.tab";
  char buffer[1024];
  char old_zone[1024];
  char *zone;

  fp = fopen(zonetable, "r");
  tmp = fopen("/tmp/zonetab.tmp", "w");
  if (fp == NULL || tmp == NULL) {
    printf("can not open the file\n");
    return CLI_ERROR;
  }
  NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  while (!feof(fp)) {
    if (buffer[0] != '#') {
      strtok(buffer, " \t");
      strtok(NULL, " \t");
      zone = strtok(NULL, " \t");
      if (zone[strlen(zone) - 1] == '\n') {
        zone[strlen(zone) - 1] = '\0';
      }
      fprintf(tmp, "%s\n", zone);
    }
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  }
  fclose(fp);
  fclose(tmp);
  remove("/tmp/zonetab");
  if (system("/bin/sort /tmp/zonetab.tmp > /tmp/zonetab") == (-1)) {
    printf("can not sort zonetab.tmp\n");
    return CLI_ERROR;
  }

  fp = fopen("/tmp/zonetab", "r");
  NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  int i = 0;
  while (!feof(fp)) {
    zone = buffer;
    if (zone[strlen(zone) - 1] == '\n') {
      zone[strlen(zone) - 1] = '\0';
    }
    if (strcmp(zone, old_zone) == 0) {
      Cli_Printf("%d   %s\n", i, zone);
    } else {
      Cli_Printf("%d   %s\n", i, zone);
    }
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
    i++;
  }
  fclose(fp);
  remove("/tmp/zonetab.tmp");
  remove("/tmp/zonetab");

  return CLI_OK;

}

// config http proxy sub-command
int
ConfigHttpProxy(int arg_ref, int setvar)
{
  Cli_Debug("ConfigHttpProxy: proxy %d\n", arg_ref);

  INKInt rmp_val = 0;
  INKInt rev_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.reverse_proxy.enabled", &rev_val);
    if (status) {
      return status;
    }
    status = Cli_RecordGetInt("proxy.config.url_remap.remap_required", &rmp_val);
    if (status) {
      return status;
    }
    if ((rev_val) && (rmp_val)) {
      Cli_Printf("rev\n");
    }
    if ((rev_val) && !(rmp_val)) {
      Cli_Printf("fwd-rev\n");
    }
    if (!rev_val) {
      Cli_Printf("fwd\n");
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_HTTP_FWD:
        status = Cli_RecordSetInt("proxy.config.reverse_proxy.enabled", (INKInt) 0, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_HTTP_REV:
        status = Cli_RecordSetInt("proxy.config.reverse_proxy.enabled", (INKInt) 1, &action_need);
        if (status) {
          return status;
        }
        status = Cli_RecordSetInt("proxy.config.url_remap.remap_required", (INKInt) 1, &action_need);

        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_HTTP_FWD_REV:
        status = Cli_RecordSetInt("proxy.config.reverse_proxy.enabled", (INKInt) 1, &action_need);
        if (status) {
          return status;
        }
        status = Cli_RecordSetInt("proxy.config.url_remap.remap_required", (INKInt) 0, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      }
      return CLI_ERROR;
    }
  default:
    return CLI_ERROR;
  }
}

// config ftp proxy sub-command
int
ConfigFtpProxy(int arg_ref, int setvar)
{
  Cli_Debug("ConfigFtpProxy: proxy %d\n", arg_ref);

  INKInt fwd_val = 0;
  INKInt rev_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.ftp.ftp_enabled", &fwd_val);
    if (status) {
      return status;
    }
    status = Cli_RecordGetInt("proxy.config.ftp.reverse_ftp_enabled", &rev_val);
    if (status) {
      return status;
    }
    if ((fwd_val) && (!rev_val))
      Cli_Printf("fwd\n");
    if ((!fwd_val) && (rev_val))
      Cli_Printf("rev\n");
    if ((fwd_val) && (rev_val))
      Cli_Printf("fwd-rev\n");
    if ((!fwd_val) && (!rev_val)) {
      Cli_Printf("invalid value retrieved\n");
      return CLI_ERROR;
    }
    return CLI_OK;


  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_FTP_FWD:
        status = Cli_RecordSetInt("proxy.config.ftp.ftp_enabled", (INKInt) 1, &action_need);
        status = Cli_RecordSetInt("proxy.config.ftp.reverse_ftp_enabled", (INKInt) 0, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_FTP_REV:
        status = Cli_RecordSetInt("proxy.config.ftp.ftp_enabled", (INKInt) 0, &action_need);
        status = Cli_RecordSetInt("proxy.config.ftp.reverse_ftp_enabled", (INKInt) 1, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_FTP_FWD_REV:
        status = Cli_RecordSetInt("proxy.config.ftp.ftp_enabled", (INKInt) 1, &action_need);
        status = Cli_RecordSetInt("proxy.config.ftp.reverse_ftp_enabled", (INKInt) 1, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      }
      return CLI_ERROR;
    }
  default:
    return CLI_ERROR;
  }
}

// config ftp mode sub-command
int
ConfigFtpMode(int arg_ref, int setvar)
{
  int mode_num = -1;
  Cli_Debug("ConfigFtpMode: mode %d\n", arg_ref);

  if (setvar == 1) {

    // convert string into mode number
    if (arg_ref == CMD_CONFIG_FTP_MODE_PASVPORT) {
      mode_num = 1;
    } else if (arg_ref == CMD_CONFIG_FTP_MODE_PORT) {
      mode_num = 2;
    } else if (arg_ref == CMD_CONFIG_FTP_MODE_PASV) {
      mode_num = 3;
    } else {
      mode_num = -1;
    }

    Cli_Debug("ConfigFtpMode: mode_num %d\n", mode_num);

    if (mode_num == -1) {
      return CLI_ERROR;
    }

    INKActionNeedT action_need = INK_ACTION_UNDEFINED;
    INKError status = Cli_RecordSetInt("proxy.config.ftp.data_connection_mode",
                                       mode_num, &action_need);
    if (status) {
      return status;
    }

    return (Cli_ConfigEnactChanges(action_need));

  } else {

    INKInt value_in = -1;
    INKError status = Cli_RecordGetInt("proxy.config.ftp.data_connection_mode",
                                       &value_in);

    if (status) {
      return status;
    }

    switch (value_in) {
    case 1:
      Cli_Printf("pasv-port\n");
      break;
    case 2:
      Cli_Printf("port\n");
      break;
    case 3:
      Cli_Printf("pasv\n");
      break;
    default:
      Cli_Printf("?\n");
      break;
    }
    return CLI_OK;
  }
}

// config ftp inactivity-timeout sub-command
int
ConfigFtpInactTimeout(int timeout, int setvar)
{
  if (setvar) {

    Cli_Debug("ConfigFtpInactTimeout: timeout %d\n", timeout);

    INKActionNeedT action_need = INK_ACTION_UNDEFINED;
    INKError status = Cli_RecordSetInt("proxy.config.ftp.control_connection_timeout",
                                       timeout, &action_need);

    if (status) {
      return status;
    }

    return (Cli_ConfigEnactChanges(action_need));

  } else {

    INKInt value_in = -1;
    INKError status = Cli_RecordGetInt("proxy.config.ftp.control_connection_timeout",
                                       &value_in);

    if (status) {
      return status;
    }

    Cli_Printf("%d\n", value_in);

    return CLI_OK;
  }
}

// config ftp expire-after sub-command
int
ConfigFtpExpireAfter(int limit, int setvar)
{
  if (setvar) {

    Cli_Debug("ConfigFtpExpireAfter: expire-after %d\n", limit);

    INKActionNeedT action_need = INK_ACTION_UNDEFINED;
    INKError status = Cli_RecordSetInt("proxy.config.http.ftp.cache.document_lifetime",
                                       limit, &action_need);

    if (status) {
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));

  } else {
    INKInt value_in = -1;
    INKError status = Cli_RecordGetInt("proxy.config.http.ftp.cache.document_lifetime",
                                       &value_in);

    if (status) {
      return status;
    }

    Cli_Printf("%d\n", value_in);

    return CLI_OK;
  }
}

// config icp mode sub-command
int
ConfigIcpMode(int arg_ref, int setvar)
{
  if (setvar) {
    int mode_num = -1;
    Cli_Debug("ConfigIcpMode: mode %d\n", arg_ref);

    // convert string into mode number
    if (arg_ref == CMD_CONFIG_ICP_MODE_DISABLED) {
      mode_num = 0;
    } else if (arg_ref == CMD_CONFIG_ICP_MODE_RECEIVE) {
      mode_num = 1;
    } else if (arg_ref == CMD_CONFIG_ICP_MODE_SENDRECEIVE) {
      mode_num = 2;
    } else {
      mode_num = -1;
    }

    Cli_Debug("ConfigIcpMode: mode_num %d\n", mode_num);

    if (mode_num == -1) {
      return CLI_ERROR;
    }

    INKActionNeedT action_need = INK_ACTION_UNDEFINED;
    INKError status = Cli_RecordSetInt("proxy.config.icp.enabled",
                                       mode_num, &action_need);
    if (status) {
      return status;
    }

    return (Cli_ConfigEnactChanges(action_need));

  } else {
    INKInt value_in = -1;
    INKError status = Cli_RecordGetInt("proxy.config.icp.enabled", &value_in);

    if (status) {
      return status;
    }

    switch (value_in) {
    case 0:
      Cli_Printf("disabled\n");
      break;
    case 1:
      Cli_Printf("receive\n");
      break;
    case 2:
      Cli_Printf("send-receive\n");
      break;
    default:
      Cli_Printf("?\n");
      break;
    }

    return CLI_OK;
  }
}

// config Cache Freshness Verify sub-command
int
ConfigCacheFreshnessVerify(int arg_ref, int setvar)
{


  Cli_Debug(" ConfigCacheFreshnessVerify: %d set?%d\n", arg_ref, setvar);

  INKInt int_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.http.cache.when_to_revalidate", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("when-expired\n");
      break;
    case 1:
      Cli_Printf("no-date\n");
      break;
    case 2:
      Cli_Printf("always\n");
      break;
    case 3:
      Cli_Printf("never\n");
      break;
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_CACHE_FRESHNESS_VERIFY_WHEN_EXPIRED:
        int_val = 0;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NO_DATE:
        int_val = 1;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_VERIFY_ALWALYS:
        int_val = 2;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_VERIFY_NEVER:
        int_val = 3;
        break;
      default:
        Cli_Printf("ERROR in Argument\n");
      }
      status = Cli_RecordSetInt("proxy.config.http.cache.when_to_revalidate", (INKInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }

  default:
    return CLI_ERROR;
  }
}

// config Cache Freshness Minimum sub-command
int
ConfigCacheFreshnessMinimum(int arg_ref, int setvar)
{

  Cli_Debug("ConfigCacheFreshnessMinimum: %d set?%d\n", arg_ref, setvar);

  INKInt int_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.http.cache.required_headers", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("nothing\n");
      break;
    case 1:
      Cli_Printf("last-modified\n");
      break;
    case 2:
      Cli_Printf("explicit\n");
      break;
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_NOTHING:
        int_val = 0;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_LAST_MODIFIED:
        int_val = 1;
        break;
      case CMD_CONFIG_CACHE_FRESHNESS_MINIMUM_EXPLICIT:
        int_val = 2;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }
      status = Cli_RecordSetInt("proxy.config.http.cache.required_headers", (INKInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
  default:
    return CLI_ERROR;
  }
}

// config Cache FreshnessNoExpireLimit 
int
ConfigCacheFreshnessNoExpireLimit(INKInt min, INKInt max, int setvar)
{

  Cli_Debug(" ConfigCacheFreshnessNoExpireLimit: greater than %d \n", min);
  Cli_Debug(" ConfigCacheFreshnessNoExpireLimit: less than %d\n", max);
  Cli_Debug(" set?%d\n", setvar);
  INKInt min_val = 0;
  INKInt max_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get
    status = Cli_RecordGetInt("proxy.config.http.cache.heuristic_min_lifetime", &min_val);
    if (status) {
      return status;
    }
    status = Cli_RecordGetInt("proxy.config.http.cache.heuristic_max_lifetime", &max_val);
    if (status) {
      return status;
    }

    Cli_Printf("greater than -- %d \n", min_val);
    Cli_Printf("less than ----- %d\n", max_val);
    return CLI_OK;
  case 1:
    status = Cli_RecordSetInt("proxy.config.http.cache.heuristic_min_lifetime", (INKInt) min, &action_need);
    if (status) {
      return status;
    }
    status = Cli_RecordSetInt("proxy.config.http.cache.heuristic_max_lifetime", (INKInt) max, &action_need);
    if (status) {
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));

  default:
    return CLI_ERROR;
  }
}

// config Cache Vary sub-command
int
ConfigCacheVary(int arg_ref, char *field, int setvar)
{

  Cli_Debug(" ConfigCacheVary: %d set?%d\n", arg_ref, setvar);
  Cli_Debug(" field: %s\n", field);
  INKString str_val = NULL;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    switch (arg_ref) {
    case CMD_CONFIG_CACHE_VARY_TEXT:
      status = Cli_RecordGetString("proxy.config.http.cache.vary_default_text", &str_val);
      break;

    case CMD_CONFIG_CACHE_VARY_COOKIES_IMAGES:
      status = Cli_RecordGetString("proxy.config.http.cache.vary_default_images", &str_val);
      break;

    case CMD_CONFIG_CACHE_VARY_OTHER:
      status = Cli_RecordGetString("proxy.config.http.cache.vary_default_other", &str_val);
      break;
    default:
      Cli_Printf(" config:cache vary <text | images | other > <field>\n");
    }
    if (status) {
      return status;
    }

    if (str_val)
      Cli_Printf("%s\n", str_val);
    else
      Cli_Printf("none\n");

    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_CACHE_VARY_TEXT:
        status = Cli_RecordSetString("proxy.config.http.cache.vary_default_text", (INKString) field, &action_need);
        break;

      case CMD_CONFIG_CACHE_VARY_COOKIES_IMAGES:
        status = Cli_RecordSetString("proxy.config.http.cache.vary_default_images", (INKString) field, &action_need);
        break;

      case CMD_CONFIG_CACHE_VARY_OTHER:
        status = Cli_RecordSetString("proxy.config.http.cache.vary_default_other", (INKString) field, &action_need);
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }

      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
  default:
    return CLI_ERROR;
  }

}

// config Cache Cookies sub-command
int
ConfigCacheCookies(int arg_ref, int setvar)
{

  Cli_Debug("ConfigCacheCookies: %d set?%d\n", arg_ref, setvar);

  INKInt int_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.http.cache.cache_responses_to_cookies", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("none\n");
      break;
    case 1:
      Cli_Printf("all\n");
      break;
    case 2:
      Cli_Printf("images\n");
      break;
    case 3:
      Cli_Printf("non-text\n");
      break;
    case 4:
      Cli_Printf("non-text-extended\n");
      break;
    default:
      Cli_Printf("ERR: invalid value fetched\n");
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_CACHE_COOKIES_NONE:
        int_val = 0;
        break;
      case CMD_CONFIG_CACHE_COOKIES_ALL:
        int_val = 1;
        break;
      case CMD_CONFIG_CACHE_VARY_COOKIES_IMAGES:
        int_val = 2;
        break;
      case CMD_CONFIG_CACHE_COOKIES_NON_TEXT:
        int_val = 3;
        break;
      case CMD_CONFIG_CACHE_COOKIES_NON_TEXT_EXT:
        int_val = 4;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }
      status = Cli_RecordSetInt("proxy.config.http.cache.cache_responses_to_cookies", (INKInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }
  default:
    return CLI_ERROR;
  }

}

// config Cache Clear sub-command
int
ConfigCacheClear()
{

  Cli_Debug("ConfigCacheClear");

  INKProxyStateT state;
  INKError status = INK_ERR_OKAY;

  state = INKProxyStateGet();
  switch (state) {
  case INK_PROXY_ON:
    Cli_Printf("Traffic Server is running.\nClear Cache failed.\n");
    return CLI_ERROR;

  case INK_PROXY_OFF:
    status = INKProxyStateSet(INK_PROXY_ON, INK_CACHE_CLEAR_ON);
    if (status) {
      return status;
    }
    return status;
  case INK_PROXY_UNDEFINED:
    Cli_Printf("Error %d: Problem clearing Cache.\n", state);
    return CLI_ERROR;
  default:
    Cli_Printf("Unexpected Proxy State\n");
    return CLI_ERROR;
  }

}


// config HostDB Clear sub-command
int
ConfigHostdbClear()
{

  Cli_Debug("ConfigHostDBClear\n");

  INKProxyStateT state = INK_PROXY_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  state = INKProxyStateGet();
  Cli_Debug("Proxy State %d\n", state);
  switch (state) {
  case INK_PROXY_ON:
    Cli_Printf("Traffic Server is running.\nClear HostDB failed.\n");
    return CLI_ERROR;

  case INK_PROXY_OFF:
    status = INKProxyStateSet(INK_PROXY_ON, INK_CACHE_CLEAR_HOSTDB);
    if (status) {
      return status;
    }
    return status;
  case INK_PROXY_UNDEFINED:
    Cli_Printf("Error %d: Problem clearing HostDB.\n", state);
    return CLI_ERROR;
  default:
    Cli_Printf("Unexpected Proxy State\n");
    return CLI_ERROR;
  }

}

//config virtualip list
int
ConfigVirtualIpList()
{

  Cli_Debug("ConfigVirtualIpList\n");

  INKCfgContext VipCtx;
  int EleCount, i;
  INKVirtIpAddrEle *VipElePtr;

  VipCtx = INKCfgContextCreate(INK_FNAME_VADDRS);
  if (INKCfgContextGet(VipCtx) != INK_ERR_OKAY) {
    Cli_Printf("ERROR READING FILE\n");
    return CLI_ERROR;
  }
  EleCount = INKCfgContextGetCount(VipCtx);
  if (EleCount == 0) {
    Cli_Printf("\nNo Virtual IP addresses specified\n");
  } else {
    Cli_Printf("\nVirtual IP addresses specified\n" "------------------------------\n", EleCount);
    for (i = 0; i < EleCount; i++) {
      VipElePtr = (INKVirtIpAddrEle *) INKCfgContextGetEleAt(VipCtx, i);
      Cli_Printf("%d) %s %s %d\n", i, VipElePtr->ip_addr, VipElePtr->intr, VipElePtr->sub_intr);
    }
  }
  Cli_Printf("\n");
  INKCfgContextDestroy(VipCtx);

  return CLI_OK;
}

//config virtualip add
int
ConfigVirtualipAdd(char *ip, char *device, int subinterface, int setvar)
{

  Cli_Debug("ConfigVirtualipAdd: %s %s %d set? %d\n", ip, device, subinterface, setvar);

  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;
  INKCfgContext VipCtx;
  int size;
  INKVirtIpAddrEle *VipElePtr;

  switch (setvar) {
  case 0:                      //get
    return (ConfigVirtualIpList());

  case 1:                      //set 

    VipElePtr = INKVirtIpAddrEleCreate();

    if (!VipElePtr)
      return CLI_ERROR;

    size = strlen(ip);
    VipElePtr->ip_addr = new char[size + 1];
    ink_strncpy(VipElePtr->ip_addr, ip, size + 1);
    size = strlen(device);
    VipElePtr->intr = new char[size + 1];
    ink_strncpy(VipElePtr->intr, device, size + 1);
    VipElePtr->sub_intr = subinterface;
    VipCtx = INKCfgContextCreate(INK_FNAME_VADDRS);
    if (INKCfgContextGet(VipCtx) != INK_ERR_OKAY)
      Cli_Printf("ERROR READING FILE\n");
    status = INKCfgContextAppendEle(VipCtx, (INKCfgEle *) VipElePtr);
    if (status) {
      Cli_Printf("ERROR %d: Failed to add entry to config file.\n", status);
      return status;
    }

    status = INKCfgContextCommit(VipCtx, &action_need, NULL);

    if (status) {
      Cli_Printf("\nERROR %d: Failed to commit changes to config file.\n"
                 "         Check parameters for correctness and try again.\n", status);
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));

  default:
    return CLI_ERROR;
  }
}

//config virtualip delete 
int
ConfigVirtualipDelete(int ip_no, int setvar)
{

  Cli_Debug("ConfigVirtualipDelete: %d set? %d\n", ip_no, setvar);

  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;
  INKCfgContext VipCtx;
  int EleCount;

  switch (setvar) {
  case 0:                      //get
    return (ConfigVirtualIpList());

  case 1:                      //set

    VipCtx = INKCfgContextCreate(INK_FNAME_VADDRS);
    if (INKCfgContextGet(VipCtx) != INK_ERR_OKAY) {
      Cli_Printf("ERROR READING FILE\n");
      return CLI_ERROR;
    }
    EleCount = INKCfgContextGetCount(VipCtx);
    if (EleCount == 0) {
      Cli_Printf("No Virual IP's to delete.\n");
      return CLI_ERROR;
    }
    if (ip_no<0 || ip_no>= EleCount) {
      if (EleCount == 1)
        Cli_Printf("ERROR: Invalid parameter %d, expected integer 0\n", ip_no);
      else
        Cli_Printf("ERROR: Invalid parameter %d, expected integer between 0 and %d\n", ip_no, EleCount - 1);

      return CLI_ERROR;
    }
    status = INKCfgContextRemoveEleAt(VipCtx, ip_no);
    if (status) {
      return status;
    }
    status = INKCfgContextCommit(VipCtx, &action_need, NULL);

    if (status) {
      Cli_Printf("\nERROR %d: Failed to commit changes to config file.\n"
                 "         Check parameters for correctness and try again.\n", status);
      return status;
    }

    return (Cli_ConfigEnactChanges(action_need));

  default:
    return CLI_ERROR;
  }
}

////////////////////////////////////////////////////////////////
// Cmd_ConfigNetwork
//
// This is the callback function for the "config:network" command.
//
// Parameters:
//    clientData -- information about parsed arguments
//    interp -- the Tcl interpreter
//    argc -- number of command arguments
//    argv -- the command arguments
//
int
Cmd_ConfigNetwork(ClientData clientData, Tcl_Interp * interp, int argc, const char *argv[])
{
  /* call to processArgForCommand must appear at the beginning
   * of each command's callback function 
   */
  if (processArgForCommand(interp, argc, argv) != CLI_OK) {
    return CMD_ERROR;
  }

  if (processHelpCommand(argc, argv) == CLI_OK)
    return CMD_OK;

  if (cliCheckIfEnabled("config:network") == CLI_ERROR) {
    return CMD_ERROR;
  }
  Cli_Debug("Cmd_ConfigNetwork argc %d\n", argc);

  cli_cmdCallbackInfo *cmdCallbackInfo;
  cli_parsedArgInfo *argtable;

  cmdCallbackInfo = (cli_cmdCallbackInfo *) clientData;
  argtable = cmdCallbackInfo->parsedArgTable;


  if (argc == 1) {
    Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
    return CMD_ERROR;
  }

  char value[256];
  char *interface;

  switch (argtable->parsed_args) {
  case CMD_CONFIG_HOSTNAME:
    Cli_Debug("CMD_CONFIG_HOSTNAME\n");
    if (argc == 2) {
      Config_GetHostname(value, sizeof(value));
      Cli_Printf("%s\n", strlen(value) ? value : "not set");
      return CLI_OK;
    } else {
      if (Config_SetHostname(argtable->arg_string)) {
        Cli_Error("ERROR while setting fully qualified hostname.\n");
        return CLI_ERROR;
      }
      return CLI_OK;
    }
    break;
  case CMD_CONFIG_DEFAULT_ROUTER:
    Cli_Debug("CMD_CONFIG_DEFAULT_ROUTER\n");
    if (argc == 2) {
      Config_GetDefaultRouter(value, sizeof(value));
      Cli_Printf("%s\n", strlen(value) ? value : "none");
      return CLI_OK;
    } else {
      if (Config_SetDefaultRouter(argtable->arg_string)) {
        Cli_Error("ERROR while setting default router.\n");
        return CLI_ERROR;
      }
      return CLI_OK;
    }
    break;
  case CMD_CONFIG_DOMAIN:
    Cli_Debug("CMD_CONFIG_DOMAIN\n");
    if (argc == 2) {
      Config_GetDomain(value, sizeof(value));
      Cli_Printf("%s\n", strlen(value) ? value : "none");
      return CLI_OK;
    } else {
      if (Config_SetDomain(argtable->arg_string)) {
        Cli_Error("ERROR while setting search-domain.\n");
        return CLI_ERROR;
      }
      return CLI_OK;
    }
  case CMD_CONFIG_DNS_IP:
    Cli_Debug("CMD_CONFIG_DNS_IP\n");
    if (argc == 2) {
      Config_GetDNS_Servers(value, sizeof(value));
      Cli_Printf("%s\n", strlen(value) ? value : "none");
      return CLI_OK;
    } else {
      if (Config_SetDNS_Servers(argtable->arg_string)) {
        Cli_Error("ERROR while setting DNS Servers.\n");
        return CLI_ERROR;
      }
      return CLI_OK;
    }
    break;
  case CMD_CONFIG_NETWORK_INT:
    Cli_Debug("CMD_CONFIG_NETWORK_INT argc %d\n", argc);
    if (argc == 2) {            // display all network interfaces
      int count = Config_GetNetworkIntCount();
      for (int i = 0; i < count; i++) {
        Config_GetNetworkInt(i, value, sizeof(value));
        Cli_Printf("%s\n", value);
      }
      return CLI_OK;
    }
    interface = argtable[0].arg_string;

    if (!Net_IsValid_Interface(interface)) {
      Cli_Printf("Invalid NIC %s\n", interface);
      return CLI_ERROR;
    }

    if (argc == 3) {            // display setting for interface
      char nic_status[80], nic_boot[80], nic_protocol[80], nic_ip[80], nic_netmask[80], nic_gateway[80];

      Config_GetNIC_Status(interface, nic_status, sizeof(nic_status));
      Config_GetNIC_Start(interface, nic_boot, sizeof(nic_boot));
      Config_GetNIC_Protocol(interface, nic_protocol, sizeof(nic_protocol));
      Config_GetNIC_IP(interface, nic_ip, sizeof(nic_ip));
      Config_GetNIC_Netmask(interface, nic_netmask, sizeof(nic_netmask));
      Config_GetNIC_Gateway(interface, nic_gateway, sizeof(nic_gateway));

      Cli_Printf("\nNIC %s\n", interface);
      Cli_Printf("  Status -------------- %s\n", nic_status);
      Cli_Printf("  Start on Boot ------- %s\n", nic_boot);
      Cli_Printf("  Start Protocol ------ %s\n", nic_protocol);
      Cli_Printf("  IP Address ---------- %s\n", nic_ip);
      Cli_Printf("  Netmask ------------- %s\n", nic_netmask);
      Cli_Printf("  Gateway ------------- %s\n", nic_gateway);
      Cli_Printf("\n");

      return CLI_OK;
    } else if (argc == 4) {
      if (strcmp(argv[3], "down") == 0) {
        if (Config_SetNIC_Down(interface)) {
          Cli_Error("ERROR while setting NIC down.\n");
          return CLI_ERROR;
        }
        return CLI_OK;
      } else {
        if ((strcmp(argv[3], "onboot") != 0)
            && (strcmp(argv[3], "not-onboot") != 0)
            && (strcmp(argv[3], "static") != 0)
            && (strcmp(argv[3], "dhcp") != 0)) {
          if (strcmp(argv[3], "up") == 0) {
            Cli_Printf("Error: Invalid syntax\n");
            Cli_Printf
              ("config:network int <interface> up <onboot | not-onboot> <static | dhcp> <ip> <netmask> <gateway>\n");
          } else {
            Cli_Printf("Invalid parameter \"%s\"\n", argv[3]);
          }
          return CLI_ERROR;
        }
        char nic_status[80];
        Config_GetNIC_Status(interface, nic_status, sizeof(nic_status));

        if (strcmp(nic_status, "down") == 0) {
          Cli_Error("ERROR: Interface must be \"up\" to make this change.\n");
          return CLI_ERROR;
        }
        if (strcmp(argv[3], "onboot") == 0 || strcmp(argv[3], "not-onboot") == 0) {
          if (Config_SetNIC_StartOnBoot(interface, (char *) argv[3])) {
            Cli_Error("ERROR: Unable to modify NIC status.\n");
            return CLI_ERROR;
          }
          return CLI_OK;
        } else if (strcmp(argv[3], "static") == 0 || strcmp(argv[3], "dhcp") == 0) {
          if (Config_SetNIC_BootProtocol(interface, (char *) argv[3])) {
            Cli_Error("ERROR: Unable to modify NIC status.\n");
            return CLI_ERROR;
          }
          return CLI_OK;
        }
      }
    } else if (argc == 5) {
      if ((strcmp(argv[3], "ip") != 0)
          && (strcmp(argv[3], "netmask") != 0)
          && (strcmp(argv[3], "gateway") != 0)) {
        Cli_Printf("Invalid parameter \"%s\"\n", argv[3]);
        return CLI_ERROR;
      }
      char nic_status[80];
      Config_GetNIC_Status(interface, nic_status, sizeof(nic_status));

      if (strcmp(nic_status, "down") == 0) {
        Cli_Error("ERROR: Interface must be \"up\" to make this change.\n");
        return CLI_ERROR;
      }
      if (strcmp(argv[3], "ip") == 0) {
        if (Config_SetNIC_IP(interface, (char *) argv[4])) {
          Cli_Error("ERROR while changing IP address.\n");
          return CLI_ERROR;
        }
        return CLI_OK;
      } else if (strcmp(argv[3], "netmask") == 0) {
        if (Config_SetNIC_Netmask(interface, (char *) argv[4])) {
          Cli_Error("ERROR while changing netmask.\n");
          return CLI_ERROR;
        }
        return CLI_OK;
      } else if (strcmp(argv[3], "gateway") == 0) {
        if (Config_SetNIC_Gateway(interface, (char *) argv[4])) {
          Cli_Error("ERROR while changing gateway.\n");
          return CLI_ERROR;
        }
        return CLI_OK;
      }
    } else if (argc == 9) {     // change setting for interface
      if (strcmp(argv[3], "up") == 0) {
        if (Config_SetNIC_Up
            (interface, (char *) argv[4], (char *) argv[5], (char *) argv[6], (char *) argv[7], (char *) argv[8])) {
          Cli_Printf("Error while changing NIC settings.\n");
          return CLI_ERROR;
        }
        return CLI_OK;
      } else {
        Cli_Printf("Invalid parameter \"%s\"\n", argv[3]);
      }
    } else {
      Cli_Printf("Invalid syntax\n");
    }
    break;
  }
  Cli_Error(ERR_COMMAND_SYNTAX, cmdCallbackInfo->command_usage);
  return CMD_ERROR;
}

int
IsValidIpAddress(char *str)
{
  char buf[50], *endPtr;
  int count = 0, gotfield = 0;
  int num;
  while (*str != 0) {
    if (!isdigit(*str) && *str != '.') {
      return CLI_ERROR;
    }

    if (isdigit(*str)) {
      buf[count] = *str;
      count++;
    }
    str++;
    if (*str == '.' || *str == 0) {
      if (count > 3) {
        return CLI_ERROR;
      }
      buf[count] = 0;
      num = strtol(buf, &endPtr, 0);
      if ((endPtr == buf) || (*endPtr != 0)) {
        return CLI_ERROR;
      }
      if (num > 255) {
        return CLI_ERROR;
      }
      gotfield++;
      count = 0;
      if (*str == 0)
        break;
      else
        str++;

    }
  }
  if (gotfield != 4) {
    return CLI_ERROR;
  }
  return CLI_OK;
}

int
IsValidHostname(char *str)
{
  while (*str != 0) {
    if (!isalnum(*str) && *str != '-' && *str != '_') {
      return CLI_ERROR;
    }
    str++;
  }
  return CLI_OK;
}

int
IsValidFQHostname(char *str)
{
  while (*str != 0) {
    if (!isalnum(*str) && *str != '-' && *str != '_' && *str != '.') {
      return CLI_ERROR;
    }
    str++;
  }
  return CLI_OK;
}

int
IsValidDomainname(char *str)
{
  while (*str != 0) {
    if (!isalnum(*str) && *str != '-' && *str != '_' && *str != '.') {
      return CLI_ERROR;
    }
    str++;
  }
  return CLI_OK;
}


int
getnameserver(char *nameserver, int len)
{
#if (HOST_OS == linux) 
  char buff[256];
  FILE *fstr;
  if ((fstr = fopen(NAMESERVER_PATH, "r")) == NULL)
    return CLI_ERROR;

  do {
    NOWARN_UNUSED_RETURN(fgets(buff, sizeof(buff), fstr));
  } while (!feof(fstr) && strncmp(buff, NAMESERVER_MARKER, strlen(NAMESERVER_MARKER)) != 0);

  if (feof(fstr)) {
    fclose(fstr);
    return CLI_ERROR;
  }

  fclose(fstr);

  strncpy(nameserver, buff + strlen(NAMESERVER_MARKER), len);

  /* Strip off any trailing newline, tabs, or blanks */
  *(nameserver + strcspn(nameserver, " \t\n")) = 0;

#endif

  return CLI_OK;

}

int
setnameserver(char *nameserver)
{
  FILE *fstr;
  if ((fstr = fopen(NAMESERVER_PATH, "wb")) == NULL) {
    return -1;
  } else {
    char domain[256];
    char resolventry[256];

#if (HOST_OS == linux)
    if (getdomainname(domain, 256) == (-1))
      return CLI_ERROR;
    snprintf((char *) &resolventry, sizeof(resolventry), "domain %s\nnameserver %s\n", domain, nameserver);
#endif

    fputs((char *) &resolventry, fstr);
    fputs("\n", fstr);
    fclose(fstr);
    return CLI_OK;
  }
  return CLI_OK;

}

int
setrouter(char *router, int len)
{
  FILE *fstr;
  if ((fstr = fopen(DEFAULTROUTER_PATH, "wb")) == NULL) {
    return -1;
  } else {
    fprintf(fstr, "%s", router);
    fclose(fstr);
    return CLI_OK;
  }
}

int
getrouter(char *router, int len)
{
  FILE *fstr;
#if (HOST_OS == linux)
  char buff[256];
  char *p;

  if ((fstr = fopen(DEFAULTROUTER_PATH, "r")) == NULL)
    return CLI_ERROR;

  do {
    NOWARN_UNUSED_RETURN(fgets(buff, sizeof(buff), fstr));
  } while (!feof(fstr) && strncmp(buff, GATEWAY_MARKER, strlen(GATEWAY_MARKER)) != 0);

  if (feof(fstr)) {
    fclose(fstr);
    return CLI_ERROR;
  }

  fclose(fstr);

  strncpy(router, buff + strlen(GATEWAY_MARKER), len);

  /* Strip off the trailing newline, if present */
  if ((p = strchr(router, '\n')) != NULL)
    *p = 0;
#endif
  return CLI_OK;

}

char *
pos_after_string(char *haystack, const char *needle)
{
  char *retval;

  retval = strstr(haystack, needle);

  if (retval != (char *) NULL)
    retval += strlen(needle);

  return retval;
}

int
getnetparms(char *ipnum, char *mask)
{
#define BUFFLEN 256
#if (HOST_OS == linux)
#define interface_name "eth0"
#endif

  FILE *ifconfig_data;
  char buff[BUFFLEN];
  char *p;

  ifconfig_data = popen("/sbin/ifconfig -a" interface_name, "r");
  if (ifconfig_data == NULL)
    return -1;


  /* We want to junk the first line. If there's no first line, then
   * the named interface doesn't exist. */
  if (fgets(buff, BUFFLEN, ifconfig_data) == NULL)
    goto err;                   /* No such interface, no data on stream */

  /* Flush to the end of the line. Should never be necessary. */
  while (buff[0] != 0 && buff[strlen(buff) - 1] != '\n')
    NOWARN_UNUSED_RETURN(fgets(buff, BUFFLEN, ifconfig_data));

  /* Get the second line. */
  if (fgets(buff, BUFFLEN, ifconfig_data) == NULL)
    goto err;                   /* Mystery, shouldn't happen. */
  if (buff[0] != 0 && buff[strlen(buff) - 1] != '\n') {
    fprintf(stderr, "Error. I can't read the output of \"ifconfig\", it's giving\n");
    fprintf(stderr, "me lines over %d characters long.\n", BUFFLEN);
    goto err;
  }
#if (HOST_OS == linux)
  p = pos_after_string(buff, "inet addr:");
#endif

  if (p != NULL) {

    while (*p && !isspace(*p)) {
      *(ipnum++) = *(p++);
    }
  } else {
    fprintf(stderr, "Error. I don't understand the output format of \"ifconfig\"\n");
    goto err;
  }
  *ipnum = 0;

  p = pos_after_string(buff, "Mask:");

  if (p != NULL) {

    while (*p && !isspace(*p)) {
      *(mask++) = *(p++);
    }
  } else {
    fprintf(stderr, "Error. I don't understand the output format of \"ifconfig\"\n");
    goto err;
  }

  *mask = 0;
  pclose(ifconfig_data);
  return CLI_OK;

err:
  pclose(ifconfig_data);
  return CLI_ERROR;
}

int
StartBinary(char *abs_bin_path, char *bin_options, int isScript)
{
  char ebuf[1024];
  unsigned char ret_value = FALSE;
  char output[1024];


  // Before we do anything lets check for the existence of
  // the binary along with it's execute permmissions
  if (access(abs_bin_path, F_OK) < 0) {
    // Error can't find binary
    snprintf(ebuf, 70, "Cannot find executable %s\n", abs_bin_path);
    Cli_Error(ebuf);
    goto done;
  }
  // binary exists, check permissions
  else if (access(abs_bin_path, R_OK | X_OK) < 0) {
    // Error don't have proper permissions
    snprintf(ebuf, 70, "Cannot execute %s\n", abs_bin_path);
    Cli_Error(ebuf);
    goto done;
  }

  memset(&output, 0, 1024);
  if (bin_options) {
    if (isScript)
      snprintf(output, 1024, "/bin/sh -x %s %s", abs_bin_path, bin_options);
    else
      snprintf(output, 1024, "%s %s", abs_bin_path, bin_options);
    ret_value = (system(output) / 256) % (UCHAR_MAX + 1);
  } else {
    ret_value = (system(abs_bin_path) / 256) % (UCHAR_MAX + 1);
  }


done:
  return ret_value;
}

////////////////////////////////////////////////////////////////
// CmdArgs_ConfigNetwork
//
// Register "config:network" arguments with the Tcl interpreter.
//
int
CmdArgs_ConfigNetwork()
{
  createArgument("hostname", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_HOSTNAME, "Hostname <string>", (char *) NULL);
  createArgument("defaultrouter", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_DEFAULT_ROUTER, "Default Router IP Address <x.x.x.x>", (char *) NULL);
  createArgument("search-domain", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_DOMAIN, "Domain-name <string>", (char *) NULL);
  createArgument("dns", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_DNS_IP, "Specify up to 3 DNS IP Addresses", (char *) NULL);
  createArgument("int", CLI_ARGV_NO_POS, CLI_ARGV_OPTION_NAME_VALUE,
                 (char *) NULL, CMD_CONFIG_NETWORK_INT, "Network Interface", (char *) NULL);
  return CLI_OK;
}

// config Logging Event sub-command
int
ConfigLoggingEvent(int arg_ref, int setvar)
{

  Cli_Debug("ConfigLoggingEvent: %d set?%d\n", arg_ref, setvar);

  INKInt int_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.log2.logging_enabled", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("disabled\n");
      break;
    case 1:
      Cli_Printf("error-only\n");
      break;
    case 2:
      Cli_Printf("trans-only\n");
      break;
    case 3:
      Cli_Printf("enabled\n");
      break;
    default:
      Cli_Printf("ERR: invalid value fetched\n");
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_LOGGING_EVENT_ENABLED:
        int_val = 3;
        break;
      case CMD_CONFIG_LOGGING_EVENT_TRANS_ONLY:
        int_val = 2;
        break;
      case CMD_CONFIG_LOGGING_EVENT_ERROR_ONLY:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_EVENT_DISABLED:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }
      status = Cli_RecordSetInt("proxy.config.log2.logging_enabled", (INKInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }

  default:
    return CLI_ERROR;
  }

}

// config Logging collation status sub-command
int
ConfigLoggingCollationStatus(int arg_ref, int setvar)
{


  Cli_Debug("ConfigLoggingCollationStatus: %d set?%d\n", arg_ref, setvar);

  INKInt int_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.local.log2.collation_mode", &int_val);
    if (status) {
      return status;
    }
    switch (int_val) {
    case 0:
      Cli_Printf("inactive\n");
      break;
    case 1:
      Cli_Printf("host\n");
      break;
    case 2:
      Cli_Printf("send-standard\n");
      break;
    case 3:
      Cli_Printf("send-custom\n");
      break;
    case 4:
      Cli_Printf("send-all\n");
      break;
    default:
      Cli_Printf("ERR: invalid value fetched\n");
    }
    return CLI_OK;

  case 1:                      //set
    {
      switch (arg_ref) {
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_INACTIVE:
        int_val = 0;
        break;
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_HOST:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_STANDARD:
        int_val = 2;
        break;
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_CUSTOM:
        int_val = 3;
        break;
      case CMD_CONFIG_LOGGING_COLLATION_STATUS_SEND_ALL:
        int_val = 4;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }
      Cli_Debug("ConfigLoggingCollationStatus: %d set?%d\n", int_val, setvar);
      status = Cli_RecordSetInt("proxy.local.log2.collation_mode", (INKInt) int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));
    }

  default:
    return CLI_ERROR;
  }
}

// config Logging collation sub-command
int
ConfigLoggingCollation(INKString secret, int arg_ref, INKInt orphan, int setvar)
{

  Cli_Debug(" LoggingCollation %s %d %d\n", secret, orphan, arg_ref);
  Cli_Debug(" set? %d\n", setvar);

  INKString str_val = NULL;
  INKInt int_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetString("proxy.config.log2.collation_secret", &str_val);
    if (status) {
      return status;
    }
    if (str_val)
      Cli_Printf("%s\n", str_val);
    else
      Cli_Printf("none\n");

    status = Cli_RecordGetInt("proxy.config.log2.collation_host_tagged", &int_val);
    if (status) {
      return status;
    }

    if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
      return CLI_ERROR;
    }

    status = Cli_RecordGetInt("proxy.config.log2.collation_port", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d MB\n", int_val);

    return CLI_OK;

  case 1:                      //set
    {

      status = Cli_RecordSetString("proxy.config.log2.collation_secret", (INKString) secret, &action_need);
      if (status) {
        return status;
      }
      switch (arg_ref) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
      }

      status = Cli_RecordSetInt("proxy.config.log2.collation_host_tagged", (INKInt) int_val, &action_need);
      if (status) {
        return status;
      }

      status = Cli_RecordSetInt("proxy.config.log2.collation_port", (INKInt) orphan, &action_need);
      if (status) {
        return status;
      }
    }
    return (Cli_ConfigEnactChanges(action_need));

  default:
    return CLI_ERROR;
  }
}


// config Logging Format Type File sub-command
int
ConfigLoggingFormatTypeFile(int arg_ref_format, int arg_ref,
                            int arg_ref_type, INKString file, INKString header, int setvar)
{

  Cli_Debug(" LoggingFormatTypeFile %d %d %d %s %s set?%d\n",
            arg_ref_format, arg_ref, arg_ref_type, file, header, setvar);

  INKString str_val = NULL;
  INKInt int_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    switch (arg_ref_format) {
    case CMD_CONFIG_LOGGING_FORMAT_SQUID:
      status = Cli_RecordGetInt("proxy.config.log2.squid_log_enabled", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      status = Cli_RecordGetInt("proxy.config.log2.squid_log_is_ascii", &int_val);
      if (status) {
        return status;
      }
      switch (int_val) {
      case 0:
        Cli_Printf("binary\n");
        break;
      case 1:
        Cli_Printf("ascii\n");
        break;
      }
      status = Cli_RecordGetString("proxy.config.log2.squid_log_name", &str_val);
      if (status) {
        return status;
      }
      if (str_val) {
        Cli_Printf("%s\n", str_val);
      } else {
        Cli_Printf("none\n");
      }

      status = Cli_RecordGetString("proxy.config.log2.squid_log_header", &str_val);
      if (status) {
        return status;
      }
      if (str_val) {
        Cli_Printf("%s\n", str_val);
      } else {
        Cli_Printf("none\n");
      }
      return CLI_OK;

    case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_COMMON:
      status = Cli_RecordGetInt("proxy.config.log2.common_log_enabled", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      status = Cli_RecordGetInt("proxy.config.log2.common_log_is_ascii", &int_val);
      if (status) {
        return status;
      }
      switch (int_val) {
      case 0:
        Cli_Printf("binary\n");
        break;
      case 1:
        Cli_Printf("ascii\n");
        break;
      }
      status = Cli_RecordGetString("proxy.config.log2.common_log_name", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      status = Cli_RecordGetString("proxy.config.log2.common_log_header", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      return CLI_OK;
    case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT:
      status = Cli_RecordGetInt("proxy.config.log2.extended_log_enabled", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      status = Cli_RecordGetInt("proxy.config.log2.extended_log_is_ascii", &int_val);
      if (status) {
        return status;
      }
      switch (int_val) {
      case 0:
        Cli_Printf("binary\n");
        break;
      case 1:
        Cli_Printf("ascii\n");
        break;
      }
      status = Cli_RecordGetString("proxy.config.log2.extended_log_name", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      status = Cli_RecordGetString("proxy.config.log2.extended_log_header", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      return CLI_OK;

    case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT2:
      status = Cli_RecordGetInt("proxy.config.log2.extended2_log_enabled", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      status = Cli_RecordGetInt("proxy.config.log2.extended2_log_is_ascii", &int_val);
      if (status) {
        return status;
      }
      switch (int_val) {
      case 0:
        Cli_Printf("binary\n");
        break;
      case 1:
        Cli_Printf("ascii\n");
        break;
      }
      status = Cli_RecordGetString("proxy.config.log2.extended2_log_name", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      status = Cli_RecordGetString("proxy.config.log2.extended2_log_header", &str_val);
      if (status) {
        return status;
      }
      Cli_Printf("%s\n", str_val);
      return CLI_OK;
    }
    break;

  case 1:                      //set
    {
      switch (arg_ref_format) {
      case CMD_CONFIG_LOGGING_FORMAT_SQUID:
        switch (arg_ref) {
        case CMD_CONFIG_LOGGING_ON:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_OFF:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log2.squid_log_enabled", (INKInt) int_val, &action_need);
        if (status) {
          return status;
        }

        switch (arg_ref_type) {
        case CMD_CONFIG_LOGGING_TYPE_ASCII:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_TYPE_BINARY:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }
        status = Cli_RecordSetInt("proxy.config.log2.squid_log_is_ascii", (INKInt) int_val, &action_need);
        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log2.squid_log_name", (INKString) file, &action_need);

        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log2.squid_log_header", (INKString) header, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_COMMON:

        switch (arg_ref) {
        case CMD_CONFIG_LOGGING_ON:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_OFF:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log2.common_log_enabled", (INKInt) int_val, &action_need);
        if (status) {
          return status;
        }

        switch (arg_ref_type) {
        case CMD_CONFIG_LOGGING_TYPE_ASCII:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_TYPE_BINARY:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log2.common_log_is_ascii", (INKInt) int_val, &action_need);
        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log2.common_log_name", (INKString) file, &action_need);

        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log2.common_log_header", (INKString) header, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));

      case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT:


        switch (arg_ref) {
        case CMD_CONFIG_LOGGING_ON:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_OFF:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log2.extended_log_enabled", (INKInt) int_val, &action_need);

        if (status) {
          return status;
        }
        switch (arg_ref_type) {
        case CMD_CONFIG_LOGGING_TYPE_ASCII:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_TYPE_BINARY:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }
        status = Cli_RecordSetInt("proxy.config.log2.extended_log_is_ascii", (INKInt) int_val, &action_need);
        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log2.extended_log_name", (INKString) file, &action_need);

        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log2.extended_log_header", (INKString) header, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      case CMD_CONFIG_LOGGING_FORMAT_NETSCAPE_EXT2:

        switch (arg_ref) {
        case CMD_CONFIG_LOGGING_ON:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_OFF:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }

        status = Cli_RecordSetInt("proxy.config.log2.extended2_log_enabled", (INKInt) int_val, &action_need);
        if (status) {
          return status;
        }
        switch (arg_ref_type) {
        case CMD_CONFIG_LOGGING_TYPE_ASCII:
          int_val = 1;
          break;
        case CMD_CONFIG_LOGGING_TYPE_BINARY:
          int_val = 0;
          break;
        default:
          Cli_Printf("ERROR in arg\n");
          return CLI_ERROR;
        }
        status = Cli_RecordSetInt("proxy.config.log2.extended2_log_is_ascii", (INKInt) int_val, &action_need);
        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log2.extended2_log_name", (INKString) file, &action_need);

        if (status) {
          return status;
        }

        status = Cli_RecordSetString("proxy.config.log2.extended2_log_header", (INKString) header, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));

      }
    }
  }
  return CLI_OK;
}

// config Logging splitting sub-command
int
ConfigLoggingSplitting(int arg_ref_protocol, int arg_ref_on_off, int setvar)
{

  Cli_Debug("ConfigLoggingSplitting %d %d set?%d\n", arg_ref_protocol, arg_ref_on_off, setvar);

  INKInt int_val;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    switch (arg_ref_protocol) {
    case CMD_CONFIG_LOGGING_SPLITTING_NNTP:

      status = Cli_RecordGetInt("proxy.config.log2.separate_nntp_logs", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      return CLI_OK;
    case CMD_CONFIG_LOGGING_SPLITTING_ICP:
      status = Cli_RecordGetInt("proxy.config.log2.separate_icp_logs", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      return CLI_OK;
    case CMD_CONFIG_LOGGING_SPLITTING_HTTP:
      status = Cli_RecordGetInt("proxy.config.log2.separate_host_logs", &int_val);
      if (status) {
        return status;
      }
      if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
        return CLI_ERROR;
      }
      return CLI_OK;
    default:
      Cli_Printf("Error in Arg\n");
      return CLI_ERROR;
    }
    return CLI_ERROR;

  case 1:
    {
      switch (arg_ref_on_off) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
        return CLI_ERROR;
      }

      switch (arg_ref_protocol) {
      case CMD_CONFIG_LOGGING_SPLITTING_NNTP:
        status = Cli_RecordSetInt("proxy.config.log2.separate_nntp_logs", int_val, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));

      case CMD_CONFIG_LOGGING_SPLITTING_ICP:
        status = Cli_RecordSetInt("proxy.config.log2.separate_icp_logs", int_val, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));

      case CMD_CONFIG_LOGGING_SPLITTING_HTTP:
        status = Cli_RecordSetInt("proxy.config.log2.separate_host_logs", int_val, &action_need);
        if (status) {
          return status;
        }
        return (Cli_ConfigEnactChanges(action_need));
      }
      return CLI_ERROR;
    }

  default:
    return CLI_ERROR;
  }
}

// config Logging Custom Format sub-command
int
ConfigLoggingCustomFormat(int arg_ref_on_off, int arg_ref_format, int setvar)
{

  Cli_Debug("ConfigLoggingSplitting %d %d set?%d\n", arg_ref_on_off, arg_ref_format, setvar);

  INKInt int_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.log2.custom_logs_enabled", &int_val);
    if (status) {
      return status;
    }
    if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
      return CLI_ERROR;
    }
    status = Cli_RecordGetInt("proxy.config.log2.xml_logs_config", &int_val);
    switch (int_val) {
    case 0:
      Cli_Printf("traditional\n");
      break;
    case 1:
      Cli_Printf("xml\n");
      break;
    }
    return CLI_OK;
  case 1:
    {
      switch (arg_ref_on_off) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
        return CLI_ERROR;
      }

      status = Cli_RecordSetInt("proxy.config.log2.custom_logs_enabled", int_val, &action_need);
      if (status) {
        return status;
      }
      switch (arg_ref_format) {
      case CMD_CONFIG_LOGGING_CUSTOM_FORMAT_TRADITIONAL:
        int_val = 0;
        break;
      case CMD_CONFIG_LOGGING_CUSTOM_FORMAT_XML:
        int_val = 1;
        break;
      }
      status = Cli_RecordSetInt("proxy.config.log2.xml_logs_config", int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));

    }

  default:
    return CLI_ERROR;
  }
}


// config Logging rolling offset interval autodelete sub-command
int
ConfigLoggingRollingOffsetIntervalAutodelete(int arg_ref_rolling,
                                             INKInt offset, INKInt num_hours, int arg_ref_auto_delete, int setvar)
{

  Cli_Debug("ConfigLoggingRollingOffsetIntervalAutodelete %d %d\n", arg_ref_rolling, offset);
  Cli_Debug("%d\n", num_hours);
  Cli_Debug("%d\n", arg_ref_auto_delete);
  Cli_Debug("set?%d\n", setvar);

  INKInt int_val = 0;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKError status = INK_ERR_OKAY;

  switch (setvar) {
  case 0:                      //get

    status = Cli_RecordGetInt("proxy.config.log2.rolling_enabled", &int_val);
    if (status) {
      return status;
    }
    if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
      return CLI_ERROR;
    }
    status = Cli_RecordGetInt("proxy.config.log2.rolling_offset_hr", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    status = Cli_RecordGetInt("proxy.config.log2.rolling_interval_sec", &int_val);
    if (status) {
      return status;
    }
    Cli_Printf("%d\n", int_val);
    status = Cli_RecordGetInt("proxy.config.log2.auto_delete_rolled_files", &int_val);
    if (status) {
      return status;
    }
    if (Cli_PrintEnable("", int_val) == CLI_ERROR) {
      return CLI_ERROR;
    }
    return CLI_OK;

  case 1:
    {
      switch (arg_ref_rolling) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
        return CLI_ERROR;
      }

      status = Cli_RecordSetInt("proxy.config.log2.rolling_enabled", int_val, &action_need);
      if (status) {
        return status;
      }
      status = Cli_RecordSetInt("proxy.config.log2.rolling_offset_hr", offset, &action_need);
      if (status) {
        return status;
      }
      status = Cli_RecordSetInt("proxy.config.log2.rolling_interval_sec", num_hours, &action_need);
      if (status) {
        return status;
      }

      switch (arg_ref_auto_delete) {
      case CMD_CONFIG_LOGGING_ON:
        int_val = 1;
        break;
      case CMD_CONFIG_LOGGING_OFF:
        int_val = 0;
        break;
      default:
        Cli_Printf("ERROR in arg\n");
        return CLI_ERROR;
      }

      status = Cli_RecordSetInt("proxy.config.log2.auto_delete_rolled_files", int_val, &action_need);
      if (status) {
        return status;
      }
      return (Cli_ConfigEnactChanges(action_need));

    }

  default:
    return CLI_ERROR;
  }
}

// config:alarm resolve-name
int
ConfigAlarmResolveName(char *name)
{
  bool active;
  INKError status;

  // determine if the event is active
  status = INKEventIsActive(name, &active);
  if (status != INK_ERR_OKAY) {
    // unable to retrieve active/inactive status for alarm
    Cli_Error(ERR_ALARM_STATUS, name);
    return CLI_ERROR;
  }

  if (!active) {
    // user tried to resolve a non-existent alarm
    Cli_Error(ERR_ALARM_RESOLVE_INACTIVE, name);
    return CLI_ERROR;
  }
  // alarm is active, resolve it
  status = INKEventResolve(name);
  if (status != INK_ERR_OKAY) {
    Cli_Error(ERR_ALARM_RESOLVE, name);
    return CLI_ERROR;
  }
  // successfully resolved alarm
  return CLI_OK;
}

// config:alarm resolve-number
int
ConfigAlarmResolveNumber(int number)
{
  INKList events;
  INKError status;
  int count, i;
  char *name = 0;

  events = INKListCreate();
  status = INKActiveEventGetMlt(events);
  if (status != INK_ERR_OKAY) {
    Cli_Error(ERR_ALARM_LIST);
    INKListDestroy(events);
    return CLI_ERROR;
  }

  count = INKListLen(events);
  if (number > count) {
    // number is too high
    Cli_Error(ERR_ALARM_RESOLVE_NUMBER, number);
    INKListDestroy(events);
    return CLI_ERROR;
  }

  for (i = 0; i < number; i++) {
    name = (char *) INKListDequeue(events);
  }

  // try to resolve the alarm
  INKListDestroy(events);
  return (ConfigAlarmResolveName(name));
}

// config:alarm resolve-all
int
ConfigAlarmResolveAll()
{
  INKList events;
  INKError status;
  int count, i;
  char *name;

  events = INKListCreate();
  status = INKActiveEventGetMlt(events);
  if (status != INK_ERR_OKAY) {
    Cli_Error(ERR_ALARM_LIST);
    INKListDestroy(events);
    return CLI_ERROR;
  }

  count = INKListLen(events);
  if (count == 0) {
    // no alarms to resolve
    Cli_Printf("No Alarms to resolve\n");
    INKListDestroy(events);
    return CLI_ERROR;
  }

  for (i = 0; i < count; i++) {
    name = (char *) INKListDequeue(events);
    status = INKEventResolve(name);
    if (status != INK_ERR_OKAY) {
      Cli_Error(ERR_ALARM_RESOLVE, name);
    }
  }

  INKListDestroy(events);
  return CLI_OK;
}

// config:alarm notify
int
ConfigAlarmNotify(char *string_val)
{
  if (string_val != NULL) {
    if (strcmp(string_val, "on") == 0) {
      AlarmCallbackPrint = 1;
      return CLI_OK;
    } else if (strcmp(string_val, "off") == 0) {
      AlarmCallbackPrint = 0;
      return CLI_OK;
    }
  } else {
    switch (AlarmCallbackPrint) {
    case 0:
      Cli_Printf("off\n");
      break;
    case 1:
      Cli_Printf("on\n");
      break;
    default:
      Cli_Printf("undefined\n");
      break;
    }
    return CLI_OK;
  }
  return CLI_ERROR;
}

int
find_value(char *pathname, char *key, char *value, int value_len, char *delim, int no)
{
  char buffer[1024];
  char *pos;
  char *open_quot, *close_quot;
  FILE *fp;
  int find = 0;
  int counter = 0;

#if (HOST_OS == linux)
  ink_strncpy(value, "", value_len);
  // coverity[fs_check_call]
  if (access(pathname, R_OK)) {
    return find;
  }
  // coverity[toctou]
  if ((fp = fopen(pathname, "r")) != NULL) {
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
    while (!feof(fp)) {
      if (strstr(buffer, key) != NULL) {
        if (counter != no) {
          counter++;
        } else {
          find = 1;
          if ((pos = strstr(buffer, delim)) != NULL) {
            pos++;
            if ((open_quot = strchr(pos, '"')) != NULL) {
              pos = open_quot + 1;
              close_quot = strrchr(pos, '"');
              *close_quot = '\0';
            }
            ink_strncpy(value, pos, value_len);

            if (value[strlen(value) - 1] == '\n') {
              value[strlen(value) - 1] = '\0';
            }
          }

          break;
        }
      }
      NOWARN_UNUSED_RETURN(fgets(buffer, 80, fp));
    }
    fclose(fp);
  }
#endif
  return find;
}

int
ConfigRadiusKeys(char *record)
{

  char new_passwd1[256], new_passwd2[256], ch = ' ';
  int i = 0;
  INKError status = INK_ERR_OKAY;
  INKActionNeedT action_need = INK_ACTION_UNDEFINED;
  INKString old_pwd_file = NULL;
  INKString dir_path = NULL;

  Cli_Debug("ConfigRadiusKeys\n");
  Cli_Printf("\nEnter New Key:");
  fflush(stdout);
  i = 0;
  ch = u_getch();
  while (ch != '\n' && ch != '\r') {
    new_passwd1[i] = ch;
    i++;
    ch = u_getch();

  }
  new_passwd1[i] = 0;

  Cli_Printf("\nReEnter New Key:");
  fflush(stdout);
  i = 0;
  ch = u_getch();
  while (ch != '\n' && ch != '\r') {
    new_passwd2[i] = ch;
    i++;
    ch = u_getch();

  }
  new_passwd2[i] = 0;

  if (strcmp(new_passwd1, new_passwd2)) {
    Cli_Printf("\nTwo New Keys Aren't the Same\n\n");
    return CMD_ERROR;
  }
  Cli_Printf("\n");
  status = Cli_RecordGetString(record, &old_pwd_file);
  if (status != INK_ERR_OKAY) {
    return CLI_ERROR;
  }
  if (old_pwd_file && strcasecmp(old_pwd_file, "NULL")) {
    // remove the old_pwd_file 
    if (remove(old_pwd_file) != 0)
      Cli_Debug("[ConfigRadiusKeys] Failed to remove password file %s", old_pwd_file);
    xfree(old_pwd_file);
  }

  Cli_RecordGetString("proxy.config.auth.password_file_path", &dir_path);
  if (!dir_path) {
    Cli_Debug("[ConfigRadiusKeys] Failed to find the password file path.");
    return CLI_ERROR;
  }

  char file_path[1025];
  time_t my_time_t;
  time(&my_time_t);
  memset(file_path, 0, 1024);
  ink_snprintf(file_path, 1024, "%s%spwd_%ld.enc", dir_path, DIR_SEP, my_time_t);
  if (dir_path)
    xfree(dir_path);

  if (INKEncryptToFile(new_passwd1, file_path) != INK_ERR_OKAY) {
    Cli_Debug("[ConfigRadiusKeys] Failed to encrypt and save the password.");
  } else {
    status = Cli_RecordSetString(record, (INKString) file_path, &action_need);
    if (status) {
      return status;
    }
    return (Cli_ConfigEnactChanges(action_need));
  }

  // Guessing that this will normally not happen, so return an error. /leif
  return CLI_ERROR;
}
