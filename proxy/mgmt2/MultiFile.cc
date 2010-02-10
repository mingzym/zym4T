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

#include "ink_config.h"
#include "ink_platform.h"
#include "ink_unused.h"    /* MAGIC_EDITING_TAG */

#include "ink_assert.h"
#include "ink_resource.h"
#include "ink_error.h"
#include "ink_file.h"
#include "InkTime.h"
#include "Compatability.h"
#include "MgmtUtils.h"
#include "MultiFile.h"
#include "ExpandingArray.h"
#include "TextBuffer.h"
#include "WebMgmtUtils.h"

/****************************************************************************
 *
 *  MultiFile.cc - base class to handle reading and displaying config
 *                 files and directories
 *  
 * 
 ****************************************************************************/

MultiFile::MultiFile()
{
  managedDir = NULL;
  dirDescript = NULL;
}

// void MultiFile::addTableEntries(ExpandingArray* fileList, textBuffer* output)
//
//   Adds table entries to output from the result of WalkFiles
//
void
MultiFile::addTableEntries(ExpandingArray * fileList, textBuffer * output)
{
  int numFiles = fileList->getNumEntries();
  fileEntry *current;
  char *safeName;
  char dateBuf[64];
  const char dataOpen[] = "\t<td>";
  const char dataClose[] = "</td>\n";
  const int dataOpenLen = strlen(dataOpen);
  const int dataCloseLen = strlen(dataClose);

  for (int i = 0; i < numFiles; i++) {
    current = (fileEntry *) ((*fileList)[i]);

    output->copyFrom("<tr>\n", 5);
    output->copyFrom(dataOpen, dataOpenLen);
    safeName = substituteForHTMLChars(current->name);
    output->copyFrom(safeName, strlen(safeName));
    delete[]safeName;
    output->copyFrom(dataClose, dataCloseLen);
    output->copyFrom(dataOpen, dataOpenLen);

    if (ink_ctime_r(&current->c_time, dateBuf) == NULL) {
      ink_strncpy(dateBuf, "<em>No time-stamp</em>", sizeof(dateBuf));
    }
    output->copyFrom(dateBuf, strlen(dateBuf));
    output->copyFrom(dataClose, dataCloseLen);
    output->copyFrom("</tr>\n", 6);
  }

}

// Mfresult MultiFile::WalkFiles(ExpandingArray* fileList) 
//
//   Iterates through the managed directory and adds every managed file
//     into the parameter snapList
//

MFresult
MultiFile::WalkFiles(ExpandingArray * fileList)
{
#ifndef _WIN32
  struct dirent *dirEntry;
  DIR *dir;
#else
  char *searchPattern;
  WIN32_FIND_DATA W32FD;
#endif
  char *fileName;
  char *filePath;
  char *records_config_filePath = NULL;
  struct stat fileInfo;
  struct stat records_config_fileInfo;
  fileEntry *fileListEntry;

#ifndef _WIN32
  if ((dir = opendir(managedDir)) == NULL) {
    mgmt_log(stderr, "[MultiFile::WalkFiles] Unable to open %s directory: %s: %s\n",
             dirDescript, managedDir, strerror(errno));
    return MF_NO_DIR;
  }
  // The fun of Solaris - readdir_r requires a buffer passed into it
  //   The man page says this obscene expression gives us the proper
  //     size
  dirEntry = (struct dirent *) xmalloc(sizeof(struct dirent) + pathconf(".", _PC_NAME_MAX) + 1);

  struct dirent *result;
  while (ink_readdir_r(dir, dirEntry, &result) == 0) {
    if (!result)
      break;
    fileName = dirEntry->d_name;
    filePath = newPathString(managedDir, fileName);
    records_config_filePath = newPathString(filePath, "records.config");
    if (stat(filePath, &fileInfo) < 0) {
      mgmt_log(stderr, "[MultiFile::WalkFiles] Stat of a %s failed %s: %s\n", dirDescript, fileName, strerror(errno));
    } else {
      if (stat(records_config_filePath, &records_config_fileInfo) < 0) {
        delete[]filePath;
        continue;
      }
      // Ignore ., .., and any dot files
      if (*fileName != '.' && isManaged(fileName)) {
        fileListEntry = (fileEntry *) xmalloc(sizeof(fileEntry));
        fileListEntry->c_time = fileInfo.st_ctime;
        ink_strncpy(fileListEntry->name, fileName, sizeof(fileListEntry->name));
        fileList->addEntry(fileListEntry);
      }
    }
    delete[]filePath;
  }

  xfree(dirEntry);
  closedir(dir);
#else
  // Append '\*' as a wildcard for FindFirstFile()
  searchPattern = newPathString(managedDir, "*");
  HANDLE hDInfo = FindFirstFile(searchPattern, &W32FD);

  if (INVALID_HANDLE_VALUE == hDInfo) {
    mgmt_log(stderr, "[MultiFile::WalkFiles] FindFirstFile failed for %s: %s\n", searchPattern, ink_last_err());
    delete[]searchPattern;
    return MF_NO_DIR;
  }
  delete[]searchPattern;

  while (FindNextFile(hDInfo, &W32FD)) {
    fileName = W32FD.cFileName;
    filePath = newPathString(managedDir, fileName);
    if (stat(filePath, &fileInfo) < 0) {
      mgmt_log(stderr, "[MultiFile::WalkFiles] Stat of a %s failed %s: %s\n", dirDescript, fileName, strerror(errno));
    } else {
      // Ignore ., .., and any dot files
      if (*fileName != '.' && isManaged(fileName)) {
        fileListEntry = (fileEntry *) xmalloc(sizeof(fileEntry));
        fileListEntry->c_time = fileInfo.st_ctime;
        strcpy(fileListEntry->name, fileName);
        fileList->addEntry(fileListEntry);
      }
    }
    delete[]filePath;
  }

  FindClose(hDInfo);
#endif

  fileList->sortWithFunction(fileEntryCmpFunc);
  delete[]records_config_filePath;
  return MF_OK;
}


bool
MultiFile::isManaged(const char *fileName)
{
  if (fileName == NULL) {
    return false;
  } else {
    return true;
  }
}

void
MultiFile::addSelectOptions(textBuffer * output, ExpandingArray * options)
{
  const char selectEnd[] = "</select>\n";
  const char option[] = "\t<option value='";
  const int optionLen = strlen(option);
  const char option_end[] = "'>";
  char *safeCurrent;

  int numOptions = options->getNumEntries();

  for (int i = 0; i < numOptions; i++) {
    output->copyFrom(option, optionLen);
    safeCurrent = substituteForHTMLChars((char *) ((*options)[i]));
    output->copyFrom(safeCurrent, strlen(safeCurrent));
    output->copyFrom(option_end, strlen(option_end));
    output->copyFrom(safeCurrent, strlen(safeCurrent));
    delete[]safeCurrent;
    output->copyFrom("\n", 1);
  }
  output->copyFrom(selectEnd, strlen(selectEnd));
}

//  int fileEntryCmpFunc(void* e1, void* e2) 
//  
//  a cmp function for fileEntry structs that can
//     used with qsort
//
//  compares c_time 
//
int
fileEntryCmpFunc(const void *e1, const void *e2)
{
  fileEntry *entry1 = (fileEntry *) * (void **) e1;
  fileEntry *entry2 = (fileEntry *) * (void **) e2;

  if (entry1->c_time > entry2->c_time) {
    return 1;
  } else if (entry1->c_time < entry2->c_time) {
    return -1;
  } else {
    return 0;
  }
}

// char* MultiFile::newPathString(const char* s1, const char* s2)
//
//   creates a new string that is composed of s1/s2
//     Callee is responsible for deleting storage
//
char *
MultiFile::newPathString(const char *s1, const char *s2)
{
  int newLen;
  char *newStr;

  newLen = strlen(s1) + strlen(s2) + 2;
  newStr = new char[newLen];
  ink_assert(newStr != NULL);
  ink_snprintf(newStr, newLen, "%s%s%s", s1, DIR_SEP, s2);
  return newStr;
}
