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

/*
 *   cliAppInit.c --
 *
 *
 *    This file initiliazes the application by calling all
 *    the new commands and thier respective arguments
 *
 *
 *
 *    Dated  : 12/13/2000.
 */
#include <stdlib.h>
#include <string.h>
// Not sure if we need to worry about old gcc compilers any more, but ... /leif
#if (__GNUC__ >= 3)
#include <iostream>
#else
#include <iostream.h>
#endif
//#include "tclExtend.h"
#include "tcl.h"
#include <unistd.h>
#include <sys/types.h>
#include "CliMgmtUtils.h"
#include "createCommand.h"
#include "hashtable.h"
#include "createArgument.h"
#include "definitions.h"
#include "ShowCmd.h"
#include "ConfigCmd.h"
#include "CliCreateCommands.h"

Tcl_Interp *interp;
extern Tcl_HashTable CommandHashtable;

int
Tcl_AppInit(Tcl_Interp * app_interp)
{
  /*
   * Intialize the Tcl interpreter.
   */
  if (Tcl_Init(app_interp) == TCL_ERROR)
    return TCL_ERROR;

  interp = app_interp;

#ifdef TCL_MEM_DEBUG
  Tcl_InitMemory(interp);
#endif

  cliCreateCommandHashtable();

  // root users are automatically enabled
  if (getuid() == 0) {
    enable_restricted_commands = TRUE;
  }


  if (CliCreateCommands() != CLI_OK)
    return CMD_ERROR;

  /*
  if (CliCreateMixtCmd() != CLI_OK)
    return CMD_ERROR;
  */

  if (CliCreateUpgradeCmd() != CLI_OK)
    return CMD_ERROR;

  Tcl_SetVar(interp, "tcl_rcFileName", "~/.tshellstartup", TCL_GLOBAL_ONLY);

/* Evaluating a application specific tcl script After creating
   all the commands and sourcing the statup file */
/* Always this should be at the end of this function. */
  /*
   * Parse command-line arguments.  A leading "-file" argument is
   * ignored (a historical relic from the distant past).  If the
   * next argument doesn't start with a "-" then strip it off and
   * use it as the name of a script file to process.
   */

  const char *fileName = Tcl_GetVar(interp, "argv", TCL_LEAVE_ERR_MSG);
  /*
   * Invoke the script specified on the command line, if any.
   */

  if (fileName[0] != '\0') {
    int listArgc;
    const char **listArgv;

    if (Tcl_SplitList(interp, fileName, &listArgc, &listArgv) != TCL_OK) {
      return TCL_ERROR;
    }
    int length = strlen(listArgv[0]);
    if ((length >= 2) && (strncmp(listArgv[0], "-file", length) == 0)) {
      for (int index = 1; index < listArgc; index++) {
        Tcl_ResetResult(interp);
        if (Tcl_EvalFile(interp, listArgv[index]) != TCL_OK) {
          Tcl_AddErrorInfo(interp, "");
          Tcl_DeleteInterp(interp);
          Tcl_Exit(1);
        }
      }
    }
    ckfree((char *) listArgv);
  }

  Tcl_ResetResult(interp);

  return TCL_OK;
}
