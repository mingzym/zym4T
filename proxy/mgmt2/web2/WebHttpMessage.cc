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

#include "ink_unused.h"    /* MAGIC_EDITING_TAG */

#include "ink_platform.h"
#include "ink_resource.h"
#include "ink_string.h"
#include "InkTime.h"
#include "WebUtils.h"
#include "WebHttpMessage.h"
#include "TextBuffer.h"
#include "MIME.h"
#include "I_Version.h"
#include "Main.h"

/****************************************************************************
 *
 *  WebHttpMessage.cc - classes to store information about incoming requests
 *                        and create hdrs for outgoing requests
 *
 *
 *
 ****************************************************************************/

const char *const httpStatStr[] = {
  "100 Continue\r\n",
  "101 Switching Protocols\r\n",
  "200 OK\r\n",
  "201 Created\r\n",
  "202 Accepted\r\n",
  "203 Non-Authorative Information\r\n",
  "204 No Content\r\n",
  "205 Reset Content\r\n",
  "206 Partial Content\r\n",
  "300 Multiple Choices\r\n",
  "301 Moved Permanently\r\n",
  "302 Moved Temporarily\r\n",
  "303 See Other\r\n",
  "304 Not Modified\r\n",
  "305 Use Proxy\r\n",
  "400 Bad Request\r\n",
  "401 Unauthorized\r\n",
  "402 Payment Required\r\n",
  "403 Forbidden\r\n",
  "404 Not Found\r\n",
  "500 Internal Server Error\r\n",
  "501 Not Implemented\r\n",
  "502 Bad Gateway\r\n",
  "503 Service Unvailable\r\n",
  "504 Gateway Timeout\r\n",
  "505 HTTP Version Not Supported\r\n"
};

const char *const httpStatCode[] = {
  "100",
  "101",
  "200",
  "201",
  "202",
  "203",
  "204",
  "205",
  "206",
  "300",
  "301",
  "302",
  "303",
  "304",
  "305",
  "400",
  "401",
  "402",
  "403",
  "404",
  "500",
  "501",
  "502",
  "503",
  "504",
  "505"
};

const char *const contentTypeStr[] = {
  "text/plain",
  "text/html",
  "text/css",
  "text/unknown",
  "image/gif",
  "image/jpeg",
  "image/png",
  "application/java-vm",
  "application/x-javascript",
  "application/x-x509-ca-cert",
  "application/x-ns-proxy-autoconfig",
  "application/zip"
};


// httpMessage::httpMessage()
//
httpMessage::httpMessage()
{

  method = METHOD_NONE;
  header = NULL;
  body = NULL;
  scheme = SCHEME_NONE;
  file = NULL;
  query = NULL;
  conLen = -1;
  referer = NULL;
  conType_str = NULL;
  authMessage = NULL;
  parser = new Tokenizer(" \t\n\r");
  modificationTime = -1;
  modContentLength = -1;
  client_request = NULL;
#ifdef OEM
  cookie = NULL;
#endif //OEM
}

void
httpMessage::getLogInfo(const char **request)
{
  *request = client_request;
}

// returns zero if everything was OK or non-zero if
// the request was malformed
int
httpMessage::addRequestLine(char *request)
{

  const char *method_str;
  const char *scheme_str;
  const char *URI;
  char *tmp;
  int requestLen;

  // Make a copy of the string so that we
  //   log it later
  client_request = xstrdup(request);
  requestLen = strlen(client_request);
  if (requestLen > 0 && client_request[requestLen - 1] == '\r') {
    client_request[requestLen - 1] = '\0';
  }

  parser->Initialize(request, SHARE_TOKS);
  method_str = (*parser)[0];
  URI = (*parser)[1];
  scheme_str = (*parser)[2];

  // Check for an empty request
  if (method_str == NULL) {
    return 1;
  }
  // Determine Method
  if (strcmp(method_str, "GET") == 0) {
    method = METHOD_GET;
  } else if (strcmp(method_str, "POST") == 0) {
    method = METHOD_POST;
  } else if (strcmp(method_str, "HEAD") == 0) {
    method = METHOD_HEAD;
  } else {
    method = METHOD_NONE;
  }

  if (URI == NULL) {
    return 1;
  }
  // Get the scheme
  //
  //  We only understand HTTP/1.0
  //
  //  If a browser asks for HTTP, we send back 1.0
  //  If there is no scheme, assume HTTP
  //  If there is another scheme, mark it unknown
  //
  if (scheme_str == NULL) {
    scheme = SCHEME_NONE;
  } else {

    if (strncasecmp(scheme_str, "HTTP", 4) == 0) {
      scheme = SCHEME_HTTP;
    } else if (strncasecmp(scheme_str, "SHTTP", 5) == 0) {
      scheme = SCHEME_SHTTP;
    } else {
      scheme = SCHEME_UNKNOWN;
    }
  }

  // Now sort out the file verses query portion of the
  // the request
  //
  // First check to see if the client sent us a full URL
  if (strncmp("http://", URI, 7) == 0) {
    URI = strstr(URI + 7, "/");
    if (URI == NULL) {
      return 1;
    }
  }
  // Now allocate space for the document path portion
  //  of the URI along with the query if any
  unsigned int amtToAllocate = strlen(URI) + 1;
  file = new char[amtToAllocate];
  ink_strncpy(file, URI, amtToAllocate);
  tmp = strstr(file, "?");
  if (tmp != NULL) {
    // There is a form submission
    *tmp = '\0';
    query = tmp + 1;
    // Remove trailing "\"
    query[strlen(query)] = '\0';
  }

  return 0;
}

void
httpMessage::addHeader(char *hdr)
{

  const char *authType;
  const char *auth;
  int len;
  const char *hdrName;
  const char *hdrArg1;

  parser->Initialize(hdr, SHARE_TOKS);
  hdrName = (*parser)[0];
  hdrArg1 = (*parser)[1];

  // All headers require at least tokens
  if (hdrName == NULL || hdrArg1 == NULL) {
    return;
  }

  if (strncasecmp("Content-length:", hdrName, 15) == 0) {
    conLen = atoi(hdrArg1);
  } else if (strncasecmp("Referer:", hdrName, 8) == 0) {
#ifdef OEM
    referer = NULL;
    if (hdrArg1 != NULL) {
#endif //OEM
      unsigned int amtToAllocate = strlen(hdrArg1);
      referer = new char[amtToAllocate + 1];
      ink_strncpy(referer, hdrArg1, amtToAllocate);
#ifdef OEM
    }
#endif //OEM
  } else if (strncasecmp("Content-type:", hdrName, 13) == 0) {
    unsigned int amtToAllocate = strlen(hdrArg1);
    conType_str = new char[amtToAllocate + 1];
    ink_strncpy(conType_str, hdrArg1, amtToAllocate);
  } else if (strncasecmp("Authorization:", hdrName, 14) == 0) {
    authType = hdrArg1;
    if (strcmp(authType, "Basic") == 0) {
      auth = (*parser)[2];
      len = strlen(auth) + 1;
      authMessage = new char[len];
      ink_strncpy(authMessage, auth, len - 1);
    }
  } else if (strncasecmp("If-Modified-Since:", hdrName, 18) == 0) {
    // Disabled due to thread safety issues
    getModDate();
#ifdef OEM
  } else if (strncasecmp("Cookie:", hdrName, 7) == 0) {
    int index = 1;
    char cookieString[1024];
    while ((hdrName = (*parser)[index++])) {
      if (strncasecmp("SessionID=", hdrName, 10) == 0) {
        sessionID = strdup((strchr(hdrName, '=') + (sizeof(char))));
#endif //OEM
      }
#ifdef OEM
      strcat(cookieString, hdrName);
#endif //OEM
    }
#ifdef OEM
    if (cookieString != NULL) {
      cookie = xstrdup(cookieString);
    }
  }
}
#endif //OEM


// httpMessage::getModDate()
//
//  A function to extract info from the If-Modified-Since http
//   header.
//
//  Currently brokens since both strptime and mktime are not thread
//   safe
//
void
httpMessage::getModDate()
{

  // Since the dates have spaces in them, we have to recontruct
  //   them.  Sigh.
  int i = 1;
  int numDateFields;
  int dateSize = 0;
  char *dateStr;
  const char *clStr;
  int tmpLen;
  Tokenizer *equalTok = NULL;

  // First, figure out the number of fields
  for (i = 1; (*parser)[i] != NULL && strstr((*parser)[i++], ";") == NULL;);
  numDateFields = i - 1;

  if (numDateFields > 0) {

    // Next, figure out the size of the string we need
    for (i = 0; i < numDateFields; i++) {
      dateSize += strlen((*parser)[i + 1]);
    }
    dateStr = new char[dateSize + 1 + numDateFields];
    *dateStr = '\0';

    // Rebuild the date string from the parsed
    //   stuff
    for (i = 0; i < numDateFields; i++) {
      strncat(dateStr, (*parser)[i + 1], dateSize + numDateFields - strlen(dateStr));
      tmpLen = strlen(dateStr);
      dateStr[tmpLen] = ' ';
      dateStr[tmpLen + 1] = '\0';
    }

    // There could be junk of the end of array like a ;
    tmpLen = strlen(dateStr);
    while (!isalpha(dateStr[tmpLen])) {
      dateStr[tmpLen] = '\0';
      tmpLen--;
    }

    modificationTime = mime_parse_date(dateStr);
    delete[]dateStr;

    // Now figure out the content length from if modified
    if (parser->getNumber() > numDateFields + 1) {
      clStr = (*parser)[numDateFields + 1];
      equalTok = new Tokenizer("=\r\n");
      equalTok->Initialize(clStr);
      if (strcasecmp("length", (*equalTok)[0]) == 0) {
        modContentLength = atoi((*equalTok)[1]);
      }
      delete equalTok;
    }
  }
}

// httpMessage::addRequestBody(int fd)
//
//  Read the request body of of the socket and make a local
//     copy of the entire thing.  Return zero if all went ok
//     or return -1 with there was an error
int
httpMessage::addRequestBody(SocketInfo socketD)
{
  char *nextRead;
  int bytesRead = 0;
  int readResult;

  if (conLen < 0) {
    return 0;
  }

  body = new char[conLen + 1];
  nextRead = body;

  while (bytesRead < conLen) {
    readResult = socket_read(socketD, nextRead, conLen - bytesRead);

    if (readResult <= 0) {
      // There was an error on the read.
      *nextRead = '\0';
      return -1;
    } else {
      bytesRead += readResult;
      nextRead += readResult;
    }
  }
  *nextRead = '\0';
  return 0;
}

httpMessage::~httpMessage()
{
  if (body != NULL) {
    delete[]body;
  }

  if (file != NULL) {
    delete[]file;
  }

  if (referer != NULL) {
    delete[]referer;
  }

  if (conType_str != NULL) {
    delete[]conType_str;
  }

  if (authMessage != NULL) {
    delete[]authMessage;
  }

  xfree(client_request);
  delete parser;
}

/* 01/14/99 elam - 
 * Commented out because g++ is not happy with iostream.h
void httpMessage::Print() {
  cout << "Method: " << method << endl;
  cout << "File: " << file << endl;
  cout << "Query: " << query << endl;
  cout << "Scheme: " << scheme << endl;
  cout << "Content Length: " << conLen << endl;
  cout << "First Header: " << header << endl;
  cout << "Message Body: " << body << endl << endl;
}
 */

httpResponse::httpResponse()
{
  // Default is no refresh header;
  refresh = -1;
  conLen = -1;
  conType = TEXT_HTML;
  explicitConType = NULL;
  refreshURL = NULL;
  lastMod = -1;
  authRealm = NULL;
  dateResponse = NULL;
  status = STATUS_INTERNAL_SERVER_ERROR;
#ifdef OEM
  cookie = NULL;
#endif //OEM
  locationURL = NULL;
#ifdef OEM
  contentLocationURL = NULL;
#endif //OEM
  cachable = 1;
}

httpResponse::~httpResponse()
{
  if (explicitConType != NULL) {
    xfree(explicitConType);
  }

  if (authRealm != NULL) {
    xfree(authRealm);
  }

  if (refreshURL != NULL) {
    xfree(refreshURL);
  }

  if (locationURL != NULL) {
    xfree(locationURL);
  }
#ifdef OEM

  if (contentLocationURL != NULL) {
    xfree(contentLocationURL);
  }
#endif //OEM

  xfree(dateResponse);
}

int
httpResponse::writeHdr(SocketInfo socketD)
{
  const char versionStr[] = "HTTP/1.0 ";
  const char serverStr[] = "Server: ";
  const char managerStr[] = "Traffic Manager ";
  const char noStoreStr[] = "Cache-Control: no-store\r\n";
  const char noCacheStr[] = "Pragma: no-cache\r\n";
  const char lenStr[] = "Content-length: ";
  const char refreshStr[] = "Refresh: ";
  const char authStr[] = "WWW-Authenticate: Basic realm=\"";
  const char dateStr[] = "Date: ";
  const char lastModStr[] = "Last-modified: ";
  const char expiresStr[] = "Expires: ";
  const char locationStr[] = "Location: ";
#ifdef OEM
  const char contentLocationStr[] = "Content-Location: ";
  const char setCookieStr[] = "Set-Cookie: ";
#endif //OEM
  const char refreshURLStr[] = "; URL=";
  time_t currentTime;
  const int bufSize = 512;
  char buffer[bufSize];
  char *reply;
  int bytesWritten;

  NOWARN_UNUSED(expiresStr);

  textBuffer hdr(4096);

  hdr.copyFrom(versionStr, strlen(versionStr));

  // Record the status
  hdr.copyFrom(httpStatStr[status], strlen(httpStatStr[status]));

  // Record Server Name
  hdr.copyFrom(serverStr, strlen(serverStr));
  hdr.copyFrom(managerStr, strlen(managerStr));
  hdr.copyFrom(appVersionInfo.VersionStr, strlen(appVersionInfo.VersionStr));
  hdr.copyFrom("\r\n", 2);

  // Record refresh
  if (refresh >= 0) {
    hdr.copyFrom(refreshStr, strlen(refreshStr));
    snprintf(buffer, bufSize - 1, "%d", refresh);
    hdr.copyFrom(buffer, strlen(buffer));
    if (refreshURL != NULL) {
      hdr.copyFrom(refreshURLStr, strlen(refreshURLStr));
      hdr.copyFrom(refreshURL, strlen(refreshURL));
    }
    hdr.copyFrom("\r\n", 2);
  }
#ifdef OEM
  if (cookie != NULL) {
    SimpleTokenizer cookiesTokens(cookie, ':');
    int cookiesCount = cookiesTokens.getNumTokensRemaining();
    for (int index = 0; index < cookiesCount; index++) {
      char *nextCookie = cookiesTokens.getNext();
      hdr.copyFrom(setCookieStr, strlen(setCookieStr));
      hdr.copyFrom(nextCookie, strlen(nextCookie));
      hdr.copyFrom(";path=/;", strlen(";path=/;"));
      //hdr.copyFrom(";expires=Wednesday, 10-Oct-2001 23:12:40 GMT", strlen(";expires=Wednesday, 10-Oct-2001 23:12:40 GMT"));
      hdr.copyFrom("\r\n", 2);
    }
  }
#endif //OEM

  // Location Header
  if (locationURL != NULL) {
    hdr.copyFrom(locationStr, strlen(locationStr));
    hdr.copyFrom(locationURL, strlen(locationURL));
    hdr.copyFrom("\r\n", 2);
  }
#ifdef OEM

  // Location Header
  if (contentLocationURL != NULL) {
    hdr.copyFrom(contentLocationStr, strlen(contentLocationStr));
    hdr.copyFrom(contentLocationURL, strlen(contentLocationURL));
    hdr.copyFrom("\r\n", 2);
  }
#endif //OEM

  // Always send the current time
  currentTime = time(NULL);
  mime_format_date(buffer, currentTime);
  // We were able to genarate the date string
  hdr.copyFrom(dateStr, strlen(dateStr));
  hdr.copyFrom(buffer, strlen(buffer));
  hdr.copyFrom("\r\n", 2);
  dateResponse = xstrdup(buffer);

  // Not cachable if marked not cachable, or has no L-M date
  if ((getCachable() == 0) || (lastMod == -1)) {
    // Use "Control-Control: no-store" for HTTP 1.1
    // compliant browsers.
    hdr.copyFrom(noStoreStr, strlen(noStoreStr));
    // For non-1.1 compliant browsers, we'll set
    // "Pragma: no-cache" just to be safe
    hdr.copyFrom(noCacheStr, strlen(noCacheStr));
  } else if (lastMod != -1) {
    // Send the last modified time if we have it
    mime_format_date(buffer, lastMod);
    // We were able to genarate the date string
    hdr.copyFrom(lastModStr, strlen(lastModStr));
    hdr.copyFrom(buffer, strlen(buffer));
    hdr.copyFrom("\r\n", 2);
  }

  snprintf(buffer, sizeof(buffer), "Content-type: %s\r\n", contentTypeStr[conType]);
  hdr.copyFrom(buffer, strlen(buffer));

  // Issue an authentication challenge if we are
  //  authorized
  if (status == STATUS_UNAUTHORIZED) {
    hdr.copyFrom(authStr, strlen(authStr));
    hdr.copyFrom(authRealm, strlen(authRealm));
    hdr.copyFrom("\"\r\n", 3);
  }

  if (conLen >= 0) {
    hdr.copyFrom(lenStr, strlen(lenStr));
    snprintf(buffer, sizeof(buffer), "%d", conLen);
    hdr.copyFrom(buffer, strlen(buffer));
    hdr.copyFrom("\r\n", 2);
  }
  // End of Header marked by empty line
  hdr.copyFrom("\r\n", 2);

  reply = hdr.bufPtr();
  bytesWritten = socket_write(socketD, reply, strlen(reply));
  return bytesWritten;
}


void
httpResponse::setContentType(const char *str)
{
  if (str != NULL) {
    explicitConType = xstrdup(str);
  }
}

void
httpResponse::setRealm(char *realm)
{
  if (realm != NULL) {
    authRealm = xstrdup(realm);
  }
}

void
httpResponse::setRefreshURL(const char *url)
{
  if (url != NULL) {
    refreshURL = xstrdup(url);
  }
}

void
httpResponse::setLocationURL(const char *url)
{
  if (url != NULL) {
    locationURL = xstrdup(url);
  }
}

#ifdef OEM
void
httpResponse::setContentLocationURL(const char *url)
{
  if (url != NULL) {
    contentLocationURL = xstrdup(url);
  }
}
#endif //OEM

void
httpResponse::getLogInfo(const char **date, HttpStatus_t * statusIn, int *length)
{
  *date = dateResponse;
  *statusIn = status;
  *length = conLen;
}
