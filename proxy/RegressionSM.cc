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

#include "P_EventSystem.h"
#include "RegressionSM.h"

#define REGRESSION_SM_RETRY (100*HRTIME_MSECOND)

void RegressionSM::set_status(int astatus) {
  ink_assert(astatus != REGRESSION_TEST_INPROGRESS);
  // INPROGRESS < NOT_RUN < PASSED < FAILED
  if (status != REGRESSION_TEST_FAILED) {
    if (status == REGRESSION_TEST_PASSED) {
      if (astatus != REGRESSION_TEST_NOT_RUN)
        status = astatus;
    } else {
      // INPROGRESS or NOT_RUN
      status = astatus;
    }
  } // else FAILED is FAILED
}

void RegressionSM::done(int astatus) {
  if (pending_action) {
    pending_action->cancel();
    pending_action = 0;
  }
  set_status(astatus);
  if (pstatus) *pstatus = status;
  if (parent) parent->child_done(status);
}

void RegressionSM::run(int *apstatus) {
  pstatus = apstatus;
  run();
}

void RegressionSM::xrun(RegressionSM *aparent) {
  parent = aparent;
  parent->nwaiting++;
  run();
}

void RegressionSM::run_in(int *apstatus, ink_hrtime t) {
  pstatus = apstatus;
  SET_HANDLER(&RegressionSM::regression_sm_start);
  eventProcessor.schedule_in(this, t);
}

void RegressionSM::child_done(int astatus) {
  { 
    MUTEX_LOCK(l, mutex, this_ethread());
    if (pending_action) {
      pending_action->cancel();
      pending_action = 0;
    }
    ink_assert(nwaiting > 0);
    --nwaiting;
    set_status(astatus);
  }
}

int RegressionSM::regression_sm_waiting(int event, void *data) {
  if (!nwaiting) {
    done(REGRESSION_TEST_NOT_RUN);
    delete this;
  }
  else
    ((Event*)data)->schedule_in(REGRESSION_SM_RETRY);
  return EVENT_CONT;
}

int RegressionSM::regression_sm_start(int event, void *data) {
  run();
  return EVENT_CONT;
}

RegressionSM *RegressionSM::do_sequential(RegressionSM *sm, ...) {
  RegressionSM *new_sm = new RegressionSM(t);
  va_list ap;
  va_start(ap, sm);
  new_sm->par = false;
  new_sm->rep = false;
  new_sm->ichild = 0;
  new_sm->nchildren = 0;
  new_sm->nwaiting = 0;
  new_sm->children(new_sm->nchildren++) = sm;
  while (1) {
    RegressionSM *x = va_arg(ap, RegressionSM*);
    if (!x) break;
    new_sm->children(new_sm->nchildren++) = x;
  }
  new_sm->n = new_sm->nchildren;
  va_end(ap);
  return new_sm;
}

RegressionSM *RegressionSM::do_sequential(int an, RegressionSM *sm) {
  RegressionSM *new_sm = new RegressionSM(t);
  new_sm->par = false;
  new_sm->rep = true;
  new_sm->ichild = 0;
  new_sm->nchildren = 1;
  new_sm->children(0) = sm;
  new_sm->nwaiting = 0;
  new_sm->n = an;
  return new_sm;
}

RegressionSM *RegressionSM::do_parallel(RegressionSM *sm, ...) {
  RegressionSM *new_sm = new RegressionSM(t);
  va_list ap;
  va_start(ap, sm);
  new_sm->par = true;
  new_sm->rep = false;
  new_sm->ichild = 0;
  new_sm->nchildren = 0;
  new_sm->nwaiting = 0;
  new_sm->children(new_sm->nchildren++) = sm;
  while (1) {
    RegressionSM *x = va_arg(ap, RegressionSM*);
    if (!x) break;
    new_sm->children(new_sm->nchildren++) = x;
  }
  new_sm->n = new_sm->nchildren;
  va_end(ap);
  return new_sm;
}

RegressionSM *RegressionSM::do_parallel(int an, RegressionSM *sm) {
  RegressionSM *new_sm = new RegressionSM(t);
  new_sm->par = true;
  new_sm->rep = true;
  new_sm->ichild = 0;
  new_sm->nchildren = 1;
  new_sm->children(0) = sm;
  new_sm->nwaiting = 0;
  new_sm->n = an;
  return new_sm;
}

void RegressionSM::run() {
  {
    MUTEX_TRY_LOCK(l, mutex, this_ethread());
    if (!l || nwaiting)
      pending_action = eventProcessor.schedule_in(this, REGRESSION_SM_RETRY);
    RegressionSM *x = 0;
    for (; ichild < n; ichild++) {
      if (!rep)
        x = children[ichild];
      else {
        if (ichild != n-1)
          x = children[0]->clone();
        else
          x = children[0];
      }
      x->xrun(this);
      if (!par && nwaiting)
        goto Lretry;
    }
  }
  if (!nwaiting) {
    done(REGRESSION_TEST_NOT_RUN);
    delete this;
    return;
  }
Lretry:
  SET_HANDLER(&RegressionSM::regression_sm_waiting);
  pending_action = eventProcessor.schedule_in(this, REGRESSION_SM_RETRY);
}

void RegressionSM::do_run(RegressionSM *sm) {
  par = false;
  rep = true;
  ichild = 0;
  nchildren = 1;
  children(0) = sm;
  nwaiting = 0;
  n = 1;
  RegressionSM::run();
}

RegressionSM::RegressionSM(const RegressionSM &ao) {
  RegressionSM &o = *(RegressionSM*)&ao;
  t = o.t;
  status = o.status;
  pstatus = o.pstatus;
  parent = &o;
  nwaiting = o.nwaiting;
  nchildren = o.nchildren;
  for (int i = 0; i < nchildren; i++)
    children(i) = o.children[i]->clone();
  n = o.n;
  ichild = o.ichild;
  par = o.par;
  rep = o.rep;
  ink_assert(status == REGRESSION_TEST_INPROGRESS);
  ink_assert(nwaiting == 0);
  ink_assert(ichild == 0);
  mutex = new_ProxyMutex();
}

struct ReRegressionSM1 : RegressionSM {
  virtual void run() {
    if (time(NULL) < 1) { // example test
      rprintf(t,"impossible");
      done(REGRESSION_TEST_FAILED);
    } else
      done(REGRESSION_TEST_PASSED);
  }
  ReRegressionSM1(RegressionTest *at) : RegressionSM(at) {}
  virtual RegressionSM *clone() { return new ReRegressionSM1(*this); }
  ReRegressionSM1(const ReRegressionSM1 &o) {
    t = o.t;
  }
};

struct ReRegressionSM2 : RegressionSM {
  virtual void run() {
    do_run(
      do_sequential(
        do_parallel(new ReRegressionSM1(t), new ReRegressionSM1(t), NULL),
        do_sequential(new ReRegressionSM1(t), new ReRegressionSM1(t), NULL),
        do_parallel(3, new ReRegressionSM1(t)),
        do_sequential(3, new ReRegressionSM1(t)),
        do_parallel(
          do_sequential(2, new ReRegressionSM1(t)),
          do_parallel(2, new ReRegressionSM1(t)),
          NULL),
        NULL));
  }
  ReRegressionSM2(RegressionTest *at) : RegressionSM(at) {}
};

REGRESSION_TEST(RegressionSM)(RegressionTest *t, int atype, int *pstatus) {
  ReRegressionSM2 *sm = new ReRegressionSM2(t); 
  sm->run_in(pstatus, HRTIME_SECOND);
}

