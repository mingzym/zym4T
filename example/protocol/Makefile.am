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

CC=@CC@
LD=ld
CFLAGS=@CFLAGS@ @SHARED_CFLAGS@
LDFLAGS=@LDFLAGS@ @SHARED_LDFLAGS@
API_INC=../../proxy/api/include

default: protocol.so

TxnSM.o: TxnSM.c TxnSM.h $(API_INC)/ts.h
	$(CC) $(CFLAGS) -I$(API_INC) -o TxnSM.o -c TxnSM.c

Protocol.o: Protocol.c $(API_INC)/ts.h
	$(CC) $(CFLAGS) -I$(API_INC) -o Protocol.o -c Protocol.c

protocol.so: Protocol.o TxnSM.o
	$(CC) $(LDFLAGS) -o protocol.so Protocol.o TxnSM.o

clean:
	rm -f protocol.so Protocol.o TxnSM.o

