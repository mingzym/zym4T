<!-------------------------------------------------------------------------
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

  ------------------------------------------------------------------------->

<@include /include/header.ink>
<@include /configure/c_header.ink>

<form method=POST action="/submit_update.cgi?<@link_query>">
<input type=hidden name=record_version value=<@record_version>>
<input type=hidden name=submit_from_page value=<@link_file>>

<table width="100%" border="0" cellspacing="0" cellpadding="0">
  <tr class="tertiaryColor"> 
    <td class="greyLinks"> 
      <p>&nbsp;&nbsp;Phone Home Configuration</p>
    </td>
  </tr>
</table>

<@include /configure/c_buttons.ink>

<@submit_error_msg>

<table width="100%" border="0" cellspacing="0" cellpadding="10"> 
  <tr>
    <td height="2" colspan="2" class="configureLabel"><@submit_error_flg proxy.config.phone_home.phone_home_enabled>Phone Home</td>
  </tr>
  <tr> 
    <td nowrap width="30%" class="bodyText"> 
      <input type="radio" name="proxy.config.phone_home.phone_home_enabled" value="2" <@checked proxy.config.phone_home.phone_home_enabled\2>>
        Enabled (Contact and Send Data)<br>
      <input type="radio" name="proxy.config.phone_home.phone_home_enabled" value="1" <@checked proxy.config.phone_home.phone_home_enabled\1>>
        Enabled (Contact Only)<br>
      <input type="radio" name="proxy.config.phone_home.phone_home_enabled" value="0" <@checked proxy.config.phone_home.phone_home_enabled\0>>
        Disabled
    </td>
    <td width="70%" class="configureHelp" valign="top" align="left"> 
      <ul>
        <li>Enables/Disables Phone Home.  Enabling Phone Home allows
        Inktomi to better understand and help our customers.
      </ul>
    </td>
  </tr>

</table>

<@include /configure/c_buttons.ink>
<@include /configure/c_footer.ink>

</form>

<@include /include/footer.ink>
