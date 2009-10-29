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

/************* ***************************
 *
 *  WebHttpContext.cc - the http web-ui transaction state
 *
 *
 ****************************************************************************/

#include "WebHttpContext.h"

//-------------------------------------------------------------------------
// WebHttpContextCreate
//
// Wraps a WebHttpContext around a WebHttpConInfo and WebHttpConInfo's
// internal WebContext.  Note that the returned WebHttpContext will
// keep pointers into the WebHttpConInfo and WebContext structures.
// Be careful not to delete/free the WebHttpConInfo or WebContext
// before WebHttpContext is done with them.
//-------------------------------------------------------------------------

WebHttpContext *
WebHttpContextCreate(WebHttpConInfo * whci)
{

  WebHttpContext *whc = (WebHttpContext *) xmalloc(sizeof(WebHttpContext));

  // memset to 0; note strings are zero'd too
  memset(whc, 0, sizeof(WebHttpContext));

  whc->current_user.access = WEB_HTTP_AUTH_ACCESS_NONE;
  whc->request = NEW(new httpMessage());
  whc->response_hdr = NEW(new httpResponse());
  whc->response_bdy = NEW(new textBuffer(8192));
  whc->submit_warn = NEW(new textBuffer(256));
  whc->submit_note = NEW(new textBuffer(256));
  whc->submit_warn_ht = ink_hash_table_create(InkHashTableKeyType_String);
  whc->submit_note_ht = ink_hash_table_create(InkHashTableKeyType_String);
  whc->si.fd = whci->fd;

  // keep pointers into the context passed to us
  whc->client_info = whci->clientInfo;
  whc->ssl_ctx = whci->context->SSL_Context;
  whc->default_file = whci->context->defaultFile;
  whc->doc_root = whci->context->docRoot;
  whc->plugin_doc_root = whci->context->pluginDocRoot;
  whc->admin_user = whci->context->admin_user;
  whc->other_users_ht = whci->context->other_users_ht;
  whc->lang_dict_ht = whci->context->lang_dict_ht;

  // set server_state
  if (whci->context->SSLenabled > 0) {
    whc->server_state |= WEB_HTTP_SERVER_STATE_SSL_ENABLED;
  }
  if (whci->context->adminAuthEnabled > 0) {
    whc->server_state |= WEB_HTTP_SERVER_STATE_AUTH_ENABLED;
  }
  if (whci->context == &autoconfContext) {
    whc->server_state |= WEB_HTTP_SERVER_STATE_AUTOCONF;
  }

  return whc;

}

//-------------------------------------------------------------------------
// WebHttpContextDestroy
//-------------------------------------------------------------------------

void
WebHttpContextDestroy(WebHttpContext * whc)
{
  if (whc) {
    if (whc->request)
      delete(whc->request);
    if (whc->response_hdr)
      delete(whc->response_hdr);
    if (whc->response_bdy)
      delete(whc->response_bdy);
    if (whc->submit_warn)
      delete(whc->submit_warn);
    if (whc->submit_note)
      delete(whc->submit_note);
    if (whc->query_data_ht)
      ink_hash_table_destroy_and_xfree_values(whc->query_data_ht);
    if (whc->post_data_ht)
      ink_hash_table_destroy_and_xfree_values(whc->post_data_ht);
    if (whc->submit_warn_ht)
      ink_hash_table_destroy(whc->submit_warn_ht);
    if (whc->submit_note_ht)
      ink_hash_table_destroy(whc->submit_note_ht);
    if (whc->top_level_render_file)
      xfree(whc->top_level_render_file);
    if (whc->cache_query_result)
      xfree(whc->cache_query_result);
    xfree(whc);
  }
}

//-------------------------------------------------------------------------
// WebHttpContextPrint_Debug
//-------------------------------------------------------------------------

void
WebHttpContextPrint_Debug(WebHttpContext * whc)
{
  if (whc) {
    printf("[WebHttpContext]\n");
    printf("-> default_file    : %s\n", whc->default_file);
    printf("-> doc_root        : %s\n", whc->doc_root);
    printf("-> plugin_doc_root : %s\n", whc->plugin_doc_root);
  }
}
