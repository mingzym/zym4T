<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method="post" action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Basic Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@nntp_plugin_status>
<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Restart</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input class="configureButton" type=submit name="restart" value=" Restart ">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Restarts <@record proxy.config.product_name> proxy and
            manager services on all nodes in the cluster.
      </ul>
    </td>
  </tr>

  <tr> 
    <td height="2" colspan="2" class="configureLabel">Clear Statistics</td>
  </tr>
  <tr>
    <td nowrap class="bodyText">
      <input class="configureButton" type=submit name="clear_stats" value=" Clear ">
    </td>
     <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Resets all <@record proxy.config.product_name> statistic counters to zero. Please allow a few seconds for the reset to take effect. 
      </ul>
    </td>
  </tr>

  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.proxy_name>Proxy Name</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText"> 
      <input type="text" size="18" name="proxy.config.proxy_name" value="<@record proxy.config.proxy_name>">
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the name of the <@record proxy.config.product_name>
            node/cluster.
        <li>In a <@record proxy.config.product_name> cluster, all nodes
            must share the same name.
      </ul>
    </td>
  </tr>

  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.alarm_email>Alarm E-Mail</td>
  </tr>
  <tr> 
    <td nowrap class="bodyText"> 
      <input type="text" size="18" name="proxy.config.alarm_email" value="<@record proxy.config.alarm_email>">
    </td>
    <td class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Specifies the e-mail address to which <@record proxy.config.product_name>
            will send alarm notifications.
      </ul>
    </td>
  </tr>
</table>

<table width="100%" border="0" cellspacing="0" cellpadding="10">
  <tr>
    <td height="2" colspan="2" class="configureLabel">Features</td>
  </tr>
  <tr>
    <td>
      <table border="1" cellspacing="0" cellpadding="3" bordercolor=#CCCCCC width="100%">
        <tr align="center"> 
          <td class="configureLabelSmall" width="100%">Feature</td>
          <td class="configureLabelSmall">On</td>
          <td class="configureLabelSmall">Off</td>
        </tr>
	<tr>
          <td height="2" colspan="3" class="configureLabelSmall">General</td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg >SNMP</td>
          <td>
            <input type="radio" name="proxy.config.snmp.master_agent_enabled:/configure/helper/snmp_enable.sh" value="1" <@checked proxy.config.snmp.master_agent_enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.snmp.master_agent_enabled:/configure/helper/snmp_enable.sh" value="0" <@checked proxy.config.snmp.master_agent_enabled\0>>
          </td>
        </tr>

	<tr>
          <td height="2" colspan="3" class="configureLabelSmall">Protocols</td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.nntp.enabled>NNTP</td>
          <td>
            <input type="radio" name="proxy.config.nntp.enabled" value="1" <@checked proxy.config.nntp.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.nntp.enabled" value="0" <@checked proxy.config.nntp.enabled\0>>
          </td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.ftp.ftp_enabled>FTP</td>
          <td>
            <input type="radio" name="proxy.config.ftp.ftp_enabled" value="1" <@checked proxy.config.ftp.ftp_enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.ftp.ftp_enabled" value="0" <@checked proxy.config.ftp.ftp_enabled\0>>
          </td>
        </tr>
	<tr>
          <td height="2" colspan="3" class="configureLabelSmall">Streaming Media</td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.qt.enabled>QuickTime</td>
          <td>
            <input type="radio" name="proxy.config.qt.enabled" value="1" <@checked proxy.config.qt.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.qt.enabled" value="0" <@checked proxy.config.qt.enabled\0>>
          </td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.rni.enabled>Real Networks</td>
          <td>
            <input type="radio" name="proxy.config.rni.enabled" value="1" <@checked proxy.config.rni.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.rni.enabled" value="0" <@checked proxy.config.rni.enabled\0>>
          </td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.wmt.enabled>Windows Media</td>
          <td>
            <input type="radio" name="proxy.config.wmt.enabled" value="1" <@checked proxy.config.wmt.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.wmt.enabled" value="0" <@checked proxy.config.wmt.enabled\0>>
          </td>
        </tr>

	<tr>
          <td height="2" colspan="3" class="configureLabelSmall">Security</td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.ldap.auth.enabled>LDAP</td>
          <td>
            <input type="radio" name="proxy.config.ldap.auth.enabled" value="1" <@checked proxy.config.ldap.auth.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.ldap.auth.enabled" value="0" <@checked proxy.config.ldap.auth.enabled\0>>
          </td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.radius.auth.enabled>Radius</td>
          <td>
            <input type="radio" name="proxy.config.radius.auth.enabled" value="1" <@checked proxy.config.radius.auth.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.radius.auth.enabled" value="0" <@checked proxy.config.radius.auth.enabled\0>>
          </td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.ntlm.auth.enabled>NTLM</td>
          <td>
            <input type="radio" name="proxy.config.ntlm.auth.enabled" value="1" <@checked proxy.config.ntlm.auth.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.ntlm.auth.enabled" value="0" <@checked proxy.config.ntlm.auth.enabled\0>>
          </td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.ssl.enabled>SSL Termination</td>
          <td>
            <input type="radio" name="proxy.config.ssl.enabled" value="1" <@checked proxy.config.ssl.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.ssl.enabled" value="0" <@checked proxy.config.ssl.enabled\0>>
          </td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.socks.socks_needed>SOCKS</td>
          <td>
            <input type="radio" name="proxy.config.socks.socks_needed" value="1" <@checked proxy.config.socks.socks_needed\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.socks.socks_needed" value="0" <@checked proxy.config.socks.socks_needed\0>> 
         </td>
        </tr>


	<tr>
          <td height="2" colspan="3" class="configureLabelSmall">Networking</td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.dns.proxy.enabled>DNS Proxy</td>
          <td class="bodyText" align=left>
            <input type="radio" name="proxy.config.dns.proxy.enabled" value="1" <@checked proxy.config.dns.proxy.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.dns.proxy.enabled" value="0" <@checked proxy.config.dns.proxy.enabled\0>>
          </td>
        </tr>
        <tr> 
          <td class="bodyText" align=left>&nbsp;&nbsp;<@submit_error_flg proxy.config.vmap.enabled>Virtual IP</td>
          <td class="bodyText" align=left>
            <input type="radio" name="proxy.config.vmap.enabled" value="1" <@checked proxy.config.vmap.enabled\1>>
          </td>
          <td>
            <input type="radio" name="proxy.config.vmap.enabled" value="0" <@checked proxy.config.vmap.enabled\0>>
          </td>
        </tr>

      </table>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

<!-- hacky: '/form' should be before c_footer.  But to make things look right, it works well to put it here.  -->
</form>

<@include /include/footer.ink>
