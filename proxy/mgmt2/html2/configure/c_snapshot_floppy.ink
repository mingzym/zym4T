<!-------------------------------------------------------------------------
  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_snapshot_floppy.cgi?<@link_query c_snapshot_floppy>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file c_snapshot_floppy>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Configuration Snapshots</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>
<@submit_error_msg>
<table width="100%" border="0" cellspacing="0" cellpadding="10"> 

<!-------------------------------------------------------------------------
  light blue bar
  ------------------------------------------------------------------------->
  <tr> 
    <td colspan="2" class="helpBg">
      <font class="configureBody">
        Snapshots allow you to save and restore
        <@record proxy.config.product_name> configurations.
	<@record proxy.config.product_name> stores snapshots on the
        node in which they were taken.  However, snapshots are
        restored to all nodes in the cluster.
      </font>
    </td>
  </tr>
  <tr> 
    <td height="2" colspan="2" class="configureLabel">List Available Snapshots on the Floppy Drive</td>
  </tr>
  <tr> 
    <td colspan="2">
      <table border="1" cellspacing="0" cellpadding="0" bordercolor=#CCCCCC width="100%">
        <tr>
          <td>
            <table border="0" cellspacing="0" cellpadding="3" width="100%">
              <tr> 
                <td height="2" colspan="2" class="configureLabelSmall">Select Floppy Drive</td>
              </tr>
              <tr>
                <td nowrap class="bodyText">
                    <input type=hidden name="selected_floppy_drive" value="<@post_data selected_floppy_drive>">
                  <table cellspacing="0" cellpadding="0" width="100%">
                    <@include_cgi /configure/helper/floppy.cgi>
                  </table>
                </td>
              </tr>
            </table>
          </td>
        </tr>
      </table>
    </td>
  </tr>
  <@select floppy_drive>
</table>
<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>
</form>
<@include /include/footer.ink>
