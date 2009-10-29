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

/***************************************/
/****************************************************************************
 *
 *  StatType.h - Functions for computing node and cluster stat
 *                          aggregation
 *
 *
 ****************************************************************************/

#ifndef _STATTYPE_H_
#define	_STATTYPE_H_

#include "StatXML.h"
#include "Main.h"               // Debug()
#include "WebMgmtUtils.h"

#define BYTES_TO_MBIT_SCALE (8/1000000.0)

#if defined MODULARIZED

#define ERROR_VALUE      0
#define StatDataT        RecDataT
#define StatFloat        RecFloat
#define StatInt          RecInt
#define StatCounter      RecCounter
#define StatString       RecString

#define STAT_INT         RECD_INT
#define STAT_FLOAT       RECD_FLOAT
#define STAT_STRING      RECD_STRING
#define STAT_COUNTER     RECD_COUNTER
#define STAT_CONST       RECD_STAT_CONST
#define STAT_FX          RECD_STAT_FX

/* Structs used in Average Statistics calculations */
struct StatFloatSamples
{
  ink_hrtime previous_time;
  ink_hrtime current_time;
  StatFloat previous_value;
  StatFloat current_value;

  StatFloat diff_value()
  {
    return (current_value - previous_value);
  }
  ink_hrtime diff_time()
  {
    return (current_time - previous_time);
  }
};

#else

#include "MgmtDefs.h"
#define ERROR_VALUE      -9999.0
#define StatDataT        MgmtType
#define StatFloat        MgmtFloat
#define StatInt          MgmtInt
#define StatCounter      MgmtIntCounter
#define StatString       MgmtString

#define STAT_INT         INK_INT
#define STAT_FLOAT       INK_FLOAT
#define STAT_STRING      INK_STRING
#define STAT_COUNTER     INK_COUNTER
#define STAT_CONST       INK_STAT_CONST
#define STAT_FX          INK_STAT_FX

#define StatFloatSamples StatTwoFloatSamples
#endif

// Urgly workaround -- no optimization in HPUX
#if (HOST_OS == hpux)
#define inline
#endif

#define MODULE      "StatPro"   // Statistics processor debug tag
#define MODULE_INIT "StatProInit"       // Statistics processor debug tag

/***************************************************************
 *                       StatExprToken
 * a statistics expression token can either be a binary operator,
 * name '+', '-', '*', '/', or parenthesis '(', ')' or a TS variable.
 * In the former case, the arithSymbol stores the operator or 
 * paranthesis; otherwise arithSymbol is '/0';
 ***************************************************************/
class StatExprToken
{

public:

  char m_arith_symbol;
  char *m_token_name;
  StatDataT m_token_type;
  StatFloat m_token_value;
  StatFloat m_token_value_max;
  StatFloat m_token_value_min;
  StatFloatSamples *m_token_value_delta;
  bool m_sum_var;
  bool m_node_var;

  // Member Functions
  void assignTokenName(const char *);
  bool assignTokenType();
  void print(char *);
  short precedence();
  void copy(const StatExprToken &);

    Link<StatExprToken> link;
    StatExprToken();
    inline ~ StatExprToken()
  {
    clean();
  };
  void clean();

  bool statVarSet(StatFloat);
};


/** 
 * StatExprList
 *   simply a list of StatExprToken.
 **/
class StatExprList
{

public:

  StatExprList();
  inline ~ StatExprList()
  {
    clean();
  };
  void clean();

  void enqueue(StatExprToken *);
  void push(StatExprToken *);
  StatExprToken *dequeue();
  StatExprToken *pop();
  StatExprToken *top();
  StatExprToken *first();
  StatExprToken *next(StatExprToken *);
  unsigned count();
  void print(char *);

private:

  size_t m_size;
  Queue<StatExprToken> m_tokenList;

};

/***************************************************************
 *                        StatObject
 * Each entry in the statistics XML file is represented by a 
 * StatObject.
 ***************************************************************/
class StatObject
{

public:

  unsigned m_id;
  bool m_debug;
  char *m_expr_string;          /* for debugging using only */
  StatExprToken *m_node_dest;
  StatExprToken *m_cluster_dest;
  StatExprList *m_expression;
  StatExprList *m_postfix;
  ink_hrtime m_last_update;
  ink_hrtime m_current_time;
  ink_hrtime m_update_interval;
  StatFloat m_stats_max;
  StatFloat m_stats_min;
  bool m_has_delta;
    Link<StatObject> link;

  // Member functions
    StatObject();
    StatObject(unsigned);
    inline ~ StatObject()
  {
    clean();
  };
  void clean();
  void assignDst(const char *, bool, bool);
  void assignExpr(const char *);

  StatExprToken *StatBinaryEval(StatExprToken *, char, StatExprToken *, bool cluster = false);
  StatFloat NodeStatEval(bool cluster);
  StatFloat ClusterStatEval();
  void setTokenValue(StatExprToken *, bool cluster = false);

private:

  void infix2postfix();

};


/**
 * StatObjectList
 *    simply a list of StatObject.
 **/
class StatObjectList
{

public:

  // Member functions
  StatObjectList();
  inline ~ StatObjectList()
  {
    clean();
  };
  void clean();
  void enqueue(StatObject * object);
  StatObject *first();
  StatObject *next(StatObject * current);
  void print(const char *prefix = "");
  short Eval();                 // return the number of statistics object processed

  size_t m_size;

private:

  Queue<StatObject> m_statList;

};

#endif
