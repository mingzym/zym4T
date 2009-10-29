<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;FTP Mapping/Redirection Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons_hide.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td width="100%" nowrap class="configureLabel" valign="top">
       <@submit_error_flg proxy.config.ftp.reverse_ftp_remap_file_name>Remapping Rules
    </td>
  </tr>
  <tr>
    <td width="100%" class="configureHelp" valign="top" align="left">
      The "<@record proxy.config.ftp.reverse_ftp_remap_file_name>"
      file lets you specify the FTP remapping rules so that
      <@record proxy.config.product_name> can direct any incoming
      FTP requests to the FTP server if the requested document is a
      cache miss or is stale.
    </td>
  </tr>
  <tr>
   <td width="100%" class="configureHelp" valign="top" align="left">
    <@config_table_object /configure/f_ftp_remap_config.ink>
   </td>
  </tr>
  <tr>
    <td colspan="2" align="right">
     <input class="configureButton" type=button name="refresh" value="Refresh" onclick="window.location='/configure/c_mapping_ftp.ink?<@link_query>'">
     <input class="configureButton" type=button name="editFile" value="Edit File" target="displayWin" onclick="window.open('/configure/submit_config_display.cgi?filename=/configure/f_ftp_remap_config.ink&fname=<@record proxy.config.ftp.reverse_ftp_remap_file_name>&frecord=proxy.config.ftp.reverse_ftp_remap_file_name', 'displayWin');">
    </td>
  </tr>

</table>

<@include /configure/c_buttons_hide.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
