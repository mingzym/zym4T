#!/usr/bin/perl -wT

#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
use strict;
use CGI;

my $q = new CGI;
my $timestamp=localtime;
my $city = $q->param ( "city" );

my $temperature=sprintf "%d", rand 100;


if ( !$city ) {
  $city = "Unknow";
}

print "Content-type: text/html\n\n";

print "<BLOCK>";
print "<BR>Temperature today in $city is $temperature<BR>";
print "<BR>Generated on <BR>$timestamp<BR>";
print "TTL is 60s<BR>";
print "</BLOCK>";
