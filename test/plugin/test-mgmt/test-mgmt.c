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

/***************************************************************
 * test-mgmt:
 * This sample plugins calls the Plugin Management APIs
 * The API covered in this plugin are - 
 * 		- INKMgmtCounterGet
 *		- INKMgmtFloatGet
 * 		- INKMgmtIntGet
 * 		- INKMgmtSringGet
 * 		- INKInstallDirGet
 *		- INKPluginDirGet
 **************************************************************/

#include <stdio.h>
#include <string.h>
#include <time.h>

#if !defined (_WIN32)
#	include <unistd.h>
#else
#	include <windows.h>
#endif

#include "InkAPI.h"
#include "macro.h"

#define STRING_SIZE 512
#define AUTO_TAG "AUTO_ERROR"
#define DEBUG_TAG "ERROR"
#define PLUGIN_NAME "test-mgmt"

const char *counterVariables[] = {
  "proxy.process.socks.connections_unsuccessful",
  "proxy.process.socks.connections_successful",
  "proxy.process.log2.log_files_open",
  "proxy.process.log2.event_log_error",
  "proxy.process.log2.event_log_access",
  "proxy.process.log2.event_log_access_fail",
  "proxy.process.log2.event_log_access_skip",
  "proxy.process.rni.object_count",
  "proxy.process.rni.block_hit_count",
  "proxy.process.rni.block_miss_count",
  '\0'
};

const char *floatVariables[] = {
  "proxy.config.admin.load_factor",
  "proxy.config.http.background_fill_completed_threshold",
  "proxy.config.http.cache.heuristic_lm_factor",
  "proxy.config.http.cache.fuzz.probability",
  "proxy.config.cache.gc.watermark",
  "proxy.config.cache.synthetic_errors.locks",
  "proxy.config.cache.synthetic_errors.disk.lseek",
  "proxy.config.cache.synthetic_errors.disk.read",
  "proxy.config.cache.synthetic_errors.disk.write",
  "proxy.config.cache.synthetic_errors.disk.readv",
  "proxy.config.cache.synthetic_errors.disk.writev",
  "proxy.config.cache.synthetic_errors.disk.pread",
  "proxy.config.cache.synthetic_errors.disk.pwrite",
  "proxy.process.nntp.transaction_totaltime.hit_fresh",
  "proxy.process.nntp.transaction_totaltime.miss_cold",
  "proxy.process.http.transaction_totaltime.hit_fresh",
  "proxy.process.http.transaction_totaltime.hit_revalidated",
  "proxy.process.http.transaction_totaltime.miss_cold",
  "proxy.process.http.transaction_totaltime.miss_changed",
  "proxy.process.http.transaction_totaltime.miss_client_no_cache",
  "proxy.process.http.transaction_totaltime.miss_not_cacheable",
  "proxy.process.http.transaction_totaltime.errors.aborts",
  "proxy.process.http.transaction_totaltime.errors.possible_aborts",
  "proxy.process.http.transaction_totaltime.errors.connect_failed",
  "proxy.process.http.transaction_totaltime.errors.pre_accept_hangups",
  "proxy.process.http.transaction_totaltime.errors.empty_hangups",
  "proxy.process.http.transaction_totaltime.errors.early_hangups",
  "proxy.process.http.transaction_totaltime.errors.other",
  "proxy.process.http.avg_transactions_per_client_connection",
  "proxy.process.http.avg_transactions_per_server_connection",
  "proxy.process.http.avg_transactions_per_parent_connection",
  "proxy.process.cluster.connections_avg_time",
  "proxy.process.cluster.control_messages_avg_send_time",
  "proxy.process.cluster.control_messages_avg_receive_time",
  "proxy.process.cluster.open_delay_time",
  "proxy.process.cluster.cache_callback_time",
  "proxy.process.cluster.rmt_cache_callback_time",
  "proxy.process.cluster.lkrmt_cache_callback_time",
  "proxy.process.cluster.local_connection_time",
  "proxy.process.cluster.remote_connection_time",
  "proxy.process.cluster.rdmsg_assemble_time",
  "proxy.process.cluster.cluster_ping_time",
  "proxy.process.cache.percent_full",
  "proxy.process.cache.gc.segments.avg_clean_time",
  "proxy.process.cache.gc.segments.avg_clear_time",
  "proxy.process.cache.reads.avg_time",
  "proxy.process.cache.reads.avg_size",
  "proxy.process.cache.flushes.avg_time",
  "proxy.process.dns.lookup_avg_time",
  "proxy.process.dns.success_avg_time",
  "proxy.process.hostdb.ttl",
  "proxy.process.icp.total_icp_response_time",
  "proxy.process.icp.total_icp_request_time",
  '\0'
};


const char *intVariables[] = {
  "proxy.local.cluster.type",
  "proxy.config.cop.core_signal",
  "proxy.config.lm.sem_id",
  "proxy.config.cluster.type",
  "proxy.config.cluster.rsport",
  "proxy.config.cluster.mcport",
  "proxy.config.cluster.mc_ttl",
  "proxy.config.cluster.log_bogus_mc_msgs",
  "proxy.config.admin.web_interface_port",
  "proxy.config.admin.autoconf_port",
  "proxy.config.admin.overseer_port",
  "proxy.config.admin.basic_auth",
  "proxy.config.admin.use_ssl",
  "proxy.config.admin.number_config_bak",
  "proxy.config.admin.ui_refresh_rate",
  "proxy.config.admin.log_mgmt_access",
  "proxy.config.admin.log_resolve_hostname",
  "proxy.config.process_manager.mgmt_port",
  "proxy.config.vmap.enabled",
  "proxy.config.vmap.mode",
  "proxy.config.phone_home.phone_home_send_info_enabled",
  "proxy.config.phone_home.phone_home_data_encryption_enabled",
  "proxy.config.phone_home.phone_home_frequency",
  "proxy.config.ldap.auth.enabled",
  "proxy.config.ldap.cache.size",
  "proxy.config.ldap.cache.storage_size",
  "proxy.config.ldap.auth.ttl_value",
  "proxy.config.ldap.auth.purge_cache_on_auth_fail",
  "proxy.config.ldap.auth.multiple.ldap_servers.enabled",
  "proxy.config.ldap.auth.bypass.enabled",
  "proxy.config.ldap.proc.ldap.server.port",
  "proxy.config.http.server_port",
  "proxy.config.http.insert_request_via_str",
  "proxy.config.http.insert_response_via_str",
  "proxy.config.http.enable_url_expandomatic",
  "proxy.config.http.no_dns_just_forward_to_parent",
  "proxy.config.http.keep_alive_enabled",
  "proxy.config.http.send_http11_requests",
  "proxy.config.http.origin_server_pipeline",
  "proxy.config.http.user_agent_pipeline",
  "proxy.config.http.share_server_sessions",
  "proxy.config.http.ftp_enabled",
  "proxy.config.http.record_heartbeat",
  "proxy.config.http.parent_proxy_routing_enable",
  "proxy.config.http.parent_proxy.retry_time",
  "proxy.config.http.parent_proxy.fail_threshold",
  "proxy.config.http.parent_proxy.total_connect_attempts",
  "proxy.config.http.parent_proxy.per_parent_connect_attempts",
  "proxy.config.http.parent_proxy.connect_attempts_timeout",
  "proxy.config.http.forward.proxy_auth_to_parent",
  "proxy.config.http.keep_alive_no_activity_timeout_in",
  "proxy.config.http.keep_alive_no_activity_timeout_out",
  "proxy.config.http.transaction_no_activity_timeout_in",
  "proxy.config.http.transaction_no_activity_timeout_out",
  "proxy.config.http.transaction_active_timeout_in",
  "proxy.config.http.transaction_active_timeout_out",
  "proxy.config.http.accept_no_activity_timeout",
  "proxy.config.http.background_fill_active_timeout",
  "proxy.config.http.connect_attempts_max_retries",
  "proxy.config.http.connect_attempts_max_retries_dead_server",
  "proxy.config.http.connect_attempts_rr_retries",
  "proxy.config.http.connect_attempts_timeout",
  "proxy.config.http.down_server.cache_time",
  "proxy.config.http.down_server.abort_threshold",
  "proxy.config.http.anonymize_remove_from",
  "proxy.config.http.anonymize_remove_referer",
  "proxy.config.http.anonymize_remove_user_agent",
  "proxy.config.http.anonymize_remove_cookie",
  "proxy.config.http.anonymize_remove_client_ip",
  "proxy.config.http.anonymize_insert_client_ip",
  "proxy.config.http.append_xforwards_header",
  "proxy.config.http.snarf_username_from_authorization",
  "proxy.config.http.insert_squid_x_forwarded_for",
  "proxy.config.http.push_method_enabled",
  "proxy.config.http.cache.http",
  "proxy.config.http.cache.ftp",
  "proxy.config.http.cache.ignore_client_no_cache",
  "proxy.config.http.cache.ims_on_client_no_cache",
  "proxy.config.http.cache.ignore_server_no_cache",
  "proxy.config.http.cache.cache_responses_to_cookies",
  "proxy.config.http.cache.ignore_authentication",
  "proxy.config.http.cache.cache_urls_that_look_dynamic",
  "proxy.config.http.cache.enable_default_vary_headers",
  "proxy.config.http.cache.when_to_revalidate",
  "proxy.config.http.cache.when_to_add_no_cache_to_msie_requests",
  "proxy.config.http.cache.required_headers",
  "proxy.config.http.cache.max_stale_age",
  "proxy.config.http.cache.add_content_length",
  "proxy.config.http.cache.range.lookup",
  "proxy.config.http.cache.heuristic_min_lifetime",
  "proxy.config.http.cache.heuristic_max_lifetime",
  "proxy.config.http.cache.guaranteed_min_lifetime",
  "proxy.config.http.cache.guaranteed_max_lifetime",
  "proxy.config.http.cache.fuzz.time",
  "proxy.config.http.ftp.cache.document_lifetime",
  "proxy.config.body_factory.enable_customizations",
  "proxy.config.body_factory.enable_logging",
  "proxy.config.body_factory.response_suppression_mode",
  "proxy.config.nntp.enabled",
  "proxy.config.nntp.cache_enabled",
  "proxy.config.nntp.posting_enabled",
  "proxy.config.nntp.access_control_enabled",
  "proxy.config.nntp.v2_authentication",
  "proxy.config.nntp.cluster_enabled",
  "proxy.config.nntp.feed_enabled",
  "proxy.config.nntp.logging_enabled",
  "proxy.config.nntp.background_posting_enabled",
  "proxy.config.nntp.insert_posting_trace_header",
  "proxy.config.nntp.server_port",
  "proxy.config.nntp.authorization_port",
  "proxy.config.nntp.obey_control_cancel",
  "proxy.config.nntp.obey_control_newgroup",
  "proxy.config.nntp.obey_control_rmgroup",
  "proxy.config.nntp.inactivity_timeout",
  "proxy.config.nntp.check_newgroups_every",
  "proxy.config.nntp.check_newnews_every",
  "proxy.config.nntp.check_cancels_every",
  "proxy.config.nntp.maintain_every",
  "proxy.config.nntp.check_pull_every",
  "proxy.config.nntp.group_check_parent_every",
  "proxy.config.nntp.group_check_parent_adapt",
  "proxy.config.nntp.group_check_cluster_every",
  "proxy.config.nntp.group_sync_every",
  "proxy.config.nntp.group_expire_every",
  "proxy.config.nntp.overview_sync_every",
  "proxy.config.nntp.overview_gc_every",
  "proxy.config.nntp.load_overview_min",
  "proxy.config.nntp.server_retry_timeout",
  "proxy.config.nntp.client_speed_throttle",
  "proxy.config.nntp.max_articles_per_group",
  "proxy.config.nntp.forward_every",
  "proxy.config.nntp.forward_backlog",
  "proxy.config.nntp.add_to_path",
  "proxy.config.nntp.forward_feed_only",
  "proxy.config.nntp.auth_on_connect",
  "proxy.config.nntp.auth_on_disconnect",
  "proxy.config.nntp.highwind_auth_compat",
  "proxy.config.nntp.send_xref_in_overviews",
  "proxy.config.nntp.run_local_authentication_server",
  "proxy.config.nntp.accept_local_authentication_requests_only",
  "proxy.config.nntp.custom_authentication_via_stdio",
  "proxy.config.ftp.data_connection_mode",
  "proxy.config.ftp.control_connection_timeout",
  "proxy.config.ftp.ftp_enabled",
  "proxy.config.ftp.cache_enabled",
  "proxy.config.ftp.logging_enabled",
  "proxy.config.ftp.proxy_server_port",
  "proxy.config.ftp.min_lisn_port",
  "proxy.config.ftp.max_lisn_port",
  "proxy.config.ftp.server_data_default_pasv",
  "proxy.config.ftp.try_pasv_times",
  "proxy.config.ftp.try_port_times",
  "proxy.config.ftp.try_server_ctrl_connect_times",
  "proxy.config.ftp.try_server_data_connect_times",
  "proxy.config.ftp.try_client_data_connect_times",
  "proxy.config.ftp.client_ctrl_no_activity_timeout",
  "proxy.config.ftp.client_ctrl_active_timeout",
  "proxy.config.ftp.server_ctrl_no_activity_timeout",
  "proxy.config.ftp.server_ctrl_active_timeout",
  "proxy.config.ftp.pasv_accept_timeout",
  "proxy.config.ftp.port_accept_timeout",
  "proxy.config.ftp.share_ftp_server_ctrl_enabled",
  "proxy.config.ftp.server_ctrl_keep_alive_no_activity_timeout",
  "proxy.config.ftp.reverse_ftp_enabled",
  "proxy.config.ftp.login_info_fresh_in_cache_time",
  "proxy.config.ftp.directory_listing_fresh_in_cache_time",
  "proxy.config.ftp.file_fresh_in_cache_time",
  "proxy.config.ftp.simple_directory_listing_cache_enabled",
  "proxy.config.ftp.full_directory_listing_cache_enabled",
  "proxy.config.socks.socks_needed",
  "proxy.config.socks.socks_version",
  "proxy.config.socks.socks_server_port",
  "proxy.config.socks.socks_timeout",
  "proxy.config.socks.accept_enabled",
  "proxy.config.net.connections_throttle",
  "proxy.config.cluster.cluster_port",
  "proxy.config.cache.permit.pinning",
  "proxy.config.cache.ram_cache.size",
  "proxy.config.cache.limits.http.max_alts",
  "proxy.config.cache.max_doc_size",
  "proxy.config.dns.search_default_domains",
  "proxy.config.dns.splitDNS.enabled",
  "proxy.config.hostdb.size",
  "proxy.config.hostdb.ttl_mode",
  "proxy.config.hostdb.timeout",
  "proxy.config.hostdb.strict_round_robin",
  "proxy.config.log2.logging_enabled",
  "proxy.config.log2.max_secs_per_buffer",
  "proxy.config.log2.max_space_mb_for_logs",
  "proxy.config.log2.max_space_mb_for_orphan_logs",
  "proxy.config.log2.max_space_mb_headroom",
  "proxy.config.log2.custom_logs_enabled",
  "proxy.config.log2.xml_logs_config",
  "proxy.config.log2.squid_log_enabled",
  "proxy.config.log2.squid_log_is_ascii",
  "proxy.config.log2.common_log_enabled",
  "proxy.config.log2.common_log_is_ascii",
  "proxy.config.log2.extended_log_enabled",
  "proxy.config.log2.extended_log_is_ascii",
  "proxy.config.log2.extended2_log_enabled",
  "proxy.config.log2.extended2_log_is_ascii",
  "proxy.config.log2.separate_icp_logs",
  "proxy.config.log2.separate_nntp_logs",
  "proxy.config.log2.separate_mixt_logs",
  "proxy.config.log2.separate_host_logs",
  "roxy.local.log2.collation_mode",
  "proxy.config.log2.collation_port",
  "proxy.config.log2.collation_host_tagged",
  "proxy.config.log2.collation_retry_sec",
  "proxy.config.log2.rolling_enabled",
  "proxy.config.log2.rolling_interval_sec",
  "proxy.config.log2.rolling_offset_hr",
  "proxy.config.log2.auto_delete_rolled_files",
  "proxy.config.log2.sampling_frequency",
  "proxy.config.qt.tcp_to_server",
  "proxy.config.qt.proxy_port",
  "proxy.config.rni.watcher_enabled",
  "proxy.config.rni.proxy_rtsp_port",
  "proxy.config.rni.proxy_port",
  "proxy.config.rni.proxy_restart_interval",
  "proxy.config.reverse_proxy.enabled",
  "proxy.config.url_remap.default_to_server_pac",
  "proxy.config.url_remap.default_to_server_pac_port",
  "proxy.config.url_remap.remap_required",
  "proxy.config.url_remap.pristine_host_hdr",
  "proxy.config.ssl.enabled",
  "proxy.config.ssl.server_port",
  "proxy.config.ssl.client.certification_level",
  "proxy.config.ssl.client.verify.server",
  "proxy.config.icp.enabled",
  "proxy.config.icp.icp_port",
  "proxy.config.icp.multicast_enabled",
  "proxy.config.icp.query_timeout",
  "proxy.config.update.enabled",
  "proxy.config.update.force",
  "proxy.config.update.retry_count",
  "proxy.config.update.retry_interval",
  "proxy.config.update.concurrent_updates",
  "proxy.config.snmp.master_agent_enabled",
  "proxy.config.diags.debug.enabled",
  "proxy.config.diags.action.enabled",
  "proxy.config.diags.show_location",
  "proxy.config.core_limit",
  '\0'
};

const char *stringVariables[] = {
  "proxy.config.proxy_name",
  "proxy.config.proxy_binary",
  "proxy.config.proxy_binary_opts",
  "proxy.config.manager_binary",
  "proxy.config.cli_binary",
  "proxy.config.watch_script",
  "proxy.config.env_prep",
  "proxy.config.config_dir",
  "proxy.config.temp_dir",
  "proxy.config.syslog_facility",
  "proxy.config.cluster.mc_group_addr",
  "proxy.config.admin.html_doc_root",
  "proxy.config.admin.admin_user",
  "proxy.config.admin.admin_password",
  "proxy.config.admin.ssl_cert_file",
  "proxy.config.admin.user_id",
  "proxy.config.alarm.bin",
  "proxy.config.alarm.abs_path",
  "proxy.config.phone_home.phone_home_server",
  "proxy.config.phone_home.phone_home_path",
  "proxy.config.phone_home.phone_home_id",
  "proxy.config.header.parse.no_host_url_redirect",
  "proxy.config.ldap.proc.ldap.server.name",
  "proxy.config.ldap.proc.ldap.base.dn",
  "proxy.config.http.server_port_attr",
  "proxy.config.http.server_other_ports",
  "proxy.config.http.ssl_ports",
  "proxy.config.http.parent_proxies",
  "proxy.config.http.anonymize_other_header_list",
  "proxy.config.http.cache.vary_default_text",
  "proxy.config.http.cache.vary_default_images",
  "proxy.config.http.cache.vary_default_other",
  "proxy.config.http.ftp.anonymous_passwd",
  "proxy.config.nntp.posting_ok_message",
  "proxy.config.nntp.posting_not_ok_message",
  "proxy.config.nntp.authorization_hostname",
  "proxy.config.nntp.auth_server.binary",
  "proxy.config.nntp.first_authorization_ip",
  "proxy.config.nntp.last_authorization_ip",
  "proxy.config.socks.socks_server_ip_str",
  "proxy.config.cluster.ethernet_interface",
  "proxy.config.dns.splitdns.def_domain",
  "proxy.config.dns.url_expansions",
  "proxy.config.log2.hostname",
  "proxy.config.log2.logfile_dir",
  "proxy.config.log2.logfile_perm",
  "proxy.config.log2.squid_log_name",
  "proxy.config.log2.squid_log_header",
  "proxy.config.log2.common_log_name",
  "proxy.config.log2.common_log_header",
  "proxy.config.log2.extended_log_name",
  "proxy.config.log2.extended_log_header",
  "proxy.config.log2.extended2_log_name",
  "proxy.config.log2.extended2_log_header",
  "proxy.config.log2.collation_host",
  "proxy.config.log2.collation_secret",
  "proxy.config.rni.proxy_pid_path",
  "proxy.config.rni.proxy_restart_cmd",
  "proxy.config.rni.proxy_service_name",
  "proxy.config.ssl.server.cert.filename",
  "proxy.config.ssl.server.cert.path",
  "proxy.config.ssl.server.private_key.filename",
  "proxy.config.ssl.server.private_key.path",
  "proxy.config.ssl.CA.cert.filename",
  "proxy.config.ssl.CA.cert.path",
  "proxy.config.ssl.client.cert.filename",
  "proxy.config.ssl.client.cert.path",
  "proxy.config.ssl.client.private_key.filename",
  "proxy.config.ssl.client.private_key.path",
  "proxy.config.ssl.client.CA.cert.filename",
  "proxy.config.ssl.client.CA.cert.path",
  "proxy.config.icp.icp_interface",
  "proxy.config.plugin.plugin_dir",
  "proxy.config.diags.debug.tags",
  "proxy.config.diags.output.diag",
  "proxy.config.diags.action.tags",
  "proxy.config.diags.output.debug",
  "proxy.config.diags.output.status",
  "proxy.config.diags.output.note",
  "proxy.config.diags.output.warning",
  "proxy.config.diags.output.error",
  "proxy.config.diags.output.fatal",
  "proxy.config.diags.output.alert",
  "proxy.config.diags.output.emergency",
  '\0'
};


static void
handleTxnStart()
{
  const char **p;
  INKMgmtCounter counterValue = 0;
  INKMgmtFloat floatValue = 0.0;
  INKMgmtInt intValue = 0;
  INKMgmtString stringValue = '\0';

  char errorLine[STRING_SIZE];

  LOG_SET_FUNCTION_NAME("handleTxnStart");


  /* Print each of the COUNTER variables */

  INKDebug(PLUGIN_NAME, "\n============= COUNTER =============");

  p = counterVariables;

  while (*p) {
    if (!INKMgmtCounterGet(*p, &counterValue)) {
      sprintf(errorLine, "ERROR: couldn't retrieve [%s] ", *p);
      LOG_AUTO_ERROR("INKMgmtCounterGet", errorLine);
    } else {
      INKDebug(PLUGIN_NAME, "%s = %lld", *p, counterValue);
    }

    *p++;
  }

  /* Print each of the FLOAT variables */

  INKDebug(PLUGIN_NAME, "\n============= FLOAT =============");

  p = floatVariables;

  while (*p) {
    if (!INKMgmtFloatGet(*p, &floatValue)) {
      sprintf(errorLine, "ERROR: couldn't retrieve [%s] ", *p);
      LOG_AUTO_ERROR("INKMgmtFloatGet", errorLine);
    } else {
      INKDebug(PLUGIN_NAME, "%s = %f", *p, floatValue);
    }

    *p++;
  }

  /* Print each of the FLOAT variables */

  INKDebug(PLUGIN_NAME, "\n============= INT =============");

  p = intVariables;

  while (*p) {
    if (!INKMgmtIntGet(*p, &intValue)) {
      sprintf(errorLine, "ERROR: couldn't retrieve [%s] ", *p);
      LOG_AUTO_ERROR("INKMgmtIntGet", errorLine);
    } else {
      INKDebug(PLUGIN_NAME, "%s = %lld", *p, intValue);
    }

    *p++;
  }

  /* Print each of the FLOAT variables */

  INKDebug(PLUGIN_NAME, "\n============= STRING =============");

  p = stringVariables;

  while (*p) {
    if (INKMgmtStringGet(*p, &stringValue)) {
      sprintf(errorLine, "ERROR: couldn't retrieve [%s] ", *p);
      LOG_AUTO_ERROR("INKMgmtStringGet", errorLine);
    } else {
      INKDebug(PLUGIN_NAME, "%s = %s", *p, stringValue);
    }

    *p++;
  }

}


void
INKPluginInit(int argc, const char *argv[])
{
  const char *ts_install_dir = INKInstallDirGet();
  const char *plugin_dir = INKPluginDirGet();

  /* Print the Traffic Server install and the plugin directory */
  printf("TS install dir: %s\n", ts_install_dir);
  printf("Plugin dir: %s\n", plugin_dir);

  handleTxnStart();
}
