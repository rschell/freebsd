/*
 *			PPP Secret Key Module
 *
 *	    Written by Toshiharu OHNO (tony-o@iij.ad.jp)
 *
 *   Copyright (C) 1994, Internet Initiative Japan, Inc. All rights reserverd.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the Internet Initiative Japan, Inc.  The name of the
 * IIJ may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * $Id: auth.c,v 1.27.2.9 1998/02/21 01:44:57 brian Exp $
 *
 *	TODO:
 *		o Implement check against with registered IP addresses.
 */
#include <sys/param.h>
#include <netinet/in.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "command.h"
#include "mbuf.h"
#include "defs.h"
#include "timer.h"
#include "fsm.h"
#include "iplist.h"
#include "throughput.h"
#include "ipcp.h"
#include "loadalias.h"
#include "vars.h"
#include "auth.h"
#include "systems.h"
#include "lcp.h"
#include "hdlc.h"
#include "async.h"
#include "link.h"
#include "descriptor.h"
#include "physical.h"
#include "chat.h"
#include "lcpproto.h"

const char *
Auth2Nam(u_short auth)
{
  switch (auth) {
  case PROTO_PAP:
    return "PAP";
  case PROTO_CHAP:
    return "CHAP";
  case 0:
    return "none";
  }
  return "unknown";
}

void
LocalAuthInit()
{
  if (!(mode&MODE_DAEMON))
    /* We're allowed in interactive mode */
    VarLocalAuth = LOCAL_AUTH;
  else if (VarHaveLocalAuthKey)
    VarLocalAuth = *VarLocalAuthKey == '\0' ? LOCAL_AUTH : LOCAL_NO_AUTH;
  else
    switch (LocalAuthValidate(SECRETFILE, VarShortHost, "")) {
    case NOT_FOUND:
      VarLocalAuth = LOCAL_DENY;
      break;
    case VALID:
      VarLocalAuth = LOCAL_AUTH;
      break;
    case INVALID:
      VarLocalAuth = LOCAL_NO_AUTH;
      break;
    }
}

LOCAL_AUTH_VALID
LocalAuthValidate(const char *fname, const char *system, const char *key)
{
  FILE *fp;
  int n;
  char *vector[3];
  char buff[LINE_LEN];
  LOCAL_AUTH_VALID rc;

  rc = NOT_FOUND;		/* No system entry */
  fp = OpenSecret(fname);
  if (fp == NULL)
    return (rc);
  while (fgets(buff, sizeof buff, fp)) {
    if (buff[0] == '#')
      continue;
    buff[strlen(buff) - 1] = 0;
    memset(vector, '\0', sizeof vector);
    n = MakeArgs(buff, vector, VECSIZE(vector));
    if (n < 1)
      continue;
    if (strcmp(vector[0], system) == 0) {
      if ((vector[1] == (char *) NULL && (key == NULL || *key == '\0')) ||
          (vector[1] != (char *) NULL && strcmp(vector[1], key) == 0)) {
	rc = VALID;		/* Valid   */
      } else {
	rc = INVALID;		/* Invalid */
      }
      break;
    }
  }
  CloseSecret(fp);
  return (rc);
}

int
AuthValidate(struct bundle *bundle, const char *fname, const char *system,
             const char *key, struct physical *physical)
{
  FILE *fp;
  int n;
  char *vector[5];
  char buff[LINE_LEN];
  char passwd[100];

  fp = OpenSecret(fname);
  if (fp == NULL)
    return (0);
  while (fgets(buff, sizeof buff, fp)) {
    if (buff[0] == '#')
      continue;
    buff[strlen(buff) - 1] = 0;
    memset(vector, '\0', sizeof vector);
    n = MakeArgs(buff, vector, VECSIZE(vector));
    if (n < 2)
      continue;
    if (strcmp(vector[0], system) == 0) {
      chat_ExpandString(NULL, vector[1], passwd, sizeof passwd, 0);
      if (strcmp(passwd, key) == 0) {
	CloseSecret(fp);
	if (n > 2 && !UseHisaddr(bundle, vector[2], 1))
	    return (0);
        /* XXX This should be deferred - we may join an existing bundle ! */
	ipcp_Setup(&IpcpInfo);
	if (n > 3)
	  SetLabel(vector[3]);
	return (1);		/* Valid */
      }
    }
  }
  CloseSecret(fp);
  return (0);			/* Invalid */
}

char *
AuthGetSecret(struct bundle *bundle, const char *fname, const char *system,
              int len, int setaddr, struct physical *physical)
{
  FILE *fp;
  int n;
  char *vector[5];
  char buff[LINE_LEN];
  static char passwd[100];

  fp = OpenSecret(fname);
  if (fp == NULL)
    return (NULL);
  while (fgets(buff, sizeof buff, fp)) {
    if (buff[0] == '#')
      continue;
    buff[strlen(buff) - 1] = 0;
    memset(vector, '\0', sizeof vector);
    n = MakeArgs(buff, vector, VECSIZE(vector));
    if (n < 2)
      continue;
    if (strlen(vector[0]) == len && strncmp(vector[0], system, len) == 0) {
      chat_ExpandString(NULL, vector[1], passwd, sizeof passwd, 0);
      if (setaddr)
	memset(&IpcpInfo.cfg.peer_range, '\0', sizeof IpcpInfo.cfg.peer_range);
      if (n > 2 && setaddr)
	if (UseHisaddr(bundle, vector[2], 1))
          /* XXX This should be deferred - we may join an existing bundle ! */
	  ipcp_Setup(&IpcpInfo);
        else
          return NULL;
      if (n > 3)
        SetLabel(vector[3]);
      return (passwd);
    }
  }
  CloseSecret(fp);
  return (NULL);		/* Invalid */
}

static void
AuthTimeout(void *vauthp)
{
  struct authinfo *authp = (struct authinfo *)vauthp;

  StopTimer(&authp->authtimer);
  if (--authp->retry > 0) {
    StartTimer(&authp->authtimer);
    (*authp->ChallengeFunc)(authp, ++authp->id, authp->physical);
  }
}

void
authinfo_Init(struct authinfo *authinfo)
{
  memset(authinfo, '\0', sizeof(struct authinfo));
}

void
StartAuthChallenge(struct authinfo *authp, struct physical *physical,
                   void (*fn)(struct authinfo *, int, struct physical *))
{
  authp->ChallengeFunc = fn;
  authp->physical = physical;
  StopTimer(&authp->authtimer);
  authp->authtimer.func = AuthTimeout;
  authp->authtimer.load = VarRetryTimeout * SECTICKS;
  authp->authtimer.state = TIMER_STOPPED;
  authp->authtimer.arg = (void *) authp;
  authp->retry = 3;
  authp->id = 1;
  (*authp->ChallengeFunc)(authp, authp->id, physical);
  StartTimer(&authp->authtimer);
}

void
StopAuthTimer(struct authinfo *authp)
{
  StopTimer(&authp->authtimer);
  authp->physical = NULL;
}
