/* Command line tool for working with NFC cards locally */
/* (c) Copyright 2022 Andrews & Arnold Ltd, Adrian Kennard */
/*
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <popt.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <ctype.h>
#include <err.h>
#include <openssl/evp.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "desfireaes.h"
#include "pn532.h"
#include <ajl.h>

int             debug = 0;      /* debug */
int             red = 33,
                amber = 32,
                green = 31;

unsigned char
gpio(int port)
{
   if (port < 0)
      port = 0 - port;
   if (port >= 30 || port <= 35)
      return (1 << (port - 30));
   if (port >= 71 || port <= 72)
      return (1 << (port - 71 + 6));
   return 0;
}

void
setled(int s, const char *led)
{                               /* Set LED */
   unsigned char   pattern = 0;
   if (led)
      for (const char *p = led; *p; p++)
         switch (toupper(*p))
         {
         case 'R':
            pattern |= gpio(red);
            break;
         case 'A':
            pattern |= gpio(amber);
            break;
         case 'G':
            pattern |= gpio(green);
            break;
         }
   if (red < 0)
      pattern ^= gpio(red);
   if (amber < 0)
      pattern ^= gpio(amber);
   if (green < 0)
      pattern ^= gpio(green);
   pn532_write_GPIO(s, pattern);
}

unsigned char  *
expecthex(const char *hex, int len, const char *name, const char *explain)
{
   if (!hex)
      return NULL;
   unsigned char  *bin = NULL;
   int             n = j_base16d(hex, &bin);
   if (n != len)
      errx(1, "--%s expects %d hexadecimal byte%s %s", name, len, (len == 1) ? "" : "s", explain ? : "");
   return bin;
}

static void
fill_random(unsigned char *buf, size_t size)
{
   //Create our random A value
   int             f = open("/dev/urandom", O_RDONLY);
   if (f < 0)
      err(1, "random");
   if (read(f, buf, size) != size)
      err(1, "random");
   close(f);
}

#define hex(name,len,explain) unsigned char *bin##name=expecthex(name,len,#name,explain)

int
main(int argc, const char *argv[])
{
   const char     *port = NULL;
   const char     *led = NULL;
   const char     *master = NULL;
   const char     *aid = NULL;
   const char     *aidkey0 = NULL;
   const char     *aidkey1 = NULL;
   int             format = 0;
   int             createaid = 0;
   int             setmaster = 0;
   int             aidkeys = 2;
   int             aidsetting = 0xEB;
   int             mastersetting = 0x09;
   int             masterconfig = 0;
   {
      poptContext     optCon;
      const struct poptOption optionsTable[] = {
         {"port", 'p', POPT_ARG_STRING, &port, 0, "Port", "/dev/cu.usbserial-..."},
         {"master", 0, POPT_ARG_STRING, &master, 0, "Master key", "Key ver and AES"},
         {"aid", 0, POPT_ARG_STRING, &aid, 0, "AID", "Application ID"},
         {"aidkey0", 0, POPT_ARG_STRING, &aidkey0, 0, "Application key 0", "Key ver and AES"},
         {"aidkey1", 0, POPT_ARG_STRING, &aidkey1, 0, "Application key 1", "Key ver and AES"},
         {"format", 0, POPT_ARG_NONE, &format, 0, "Format card"},
         {"create-aid", 0, POPT_ARG_NONE, &createaid, 0, "Create AID"},
         {"set-master", 0, POPT_ARG_NONE, &setmaster, 0, "Set a master key"},
         {"master-setting", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &mastersetting, 0, "Master key setting", "N"},
         {"master-config", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &masterconfig, 0, "Master key config", "N"},
         {"aid-keys", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &aidkeys, 0, "AID keys", "N"},
         {"aid-setting", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &aidsetting, 0, "AID setting", "N"},
         {"red", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &red, 0, "Red port", "30/31/32/33/34/5/71/72"},
         {"amber", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &amber, 0, "Amber port", "30/31/32/33/34/5/71/72"},
         {"green", 0, POPT_ARG_INT | POPT_ARGFLAG_SHOW_DEFAULT, &green, 0, "Green port", "30/31/32/33/34/5/71/72"},
         {"led", 0, POPT_ARG_STRING, &led, 0, "LED", "R/A/G"},
         {"debug", 'v', POPT_ARG_NONE, &debug, 0, "Debug"},
         POPT_AUTOHELP {}
      };

      optCon = poptGetContext(NULL, argc, argv, optionsTable, 0);
      //poptSetOtherOptionHelp(optCon, "");

      int             c;
      if ((c = poptGetNextOpt(optCon)) < -1)
         errx(1, "%s: %s\n", poptBadOption(optCon, POPT_BADOPTION_NOALIAS), poptStrerror(c));

      if (!port && poptPeekArg(optCon))
         port = poptGetArg(optCon);

      if (poptPeekArg(optCon) || !port)
      {
         poptPrintUsage(optCon, stderr, 0);
         return -1;
      }
      poptFreeContext(optCon);
   }
   hex(master, 17, "Key version and 16 byte AES key data");
   hex(aid, 3, "Application ID");
   hex(aidkey0, 17, "Key version and 16 byte AES key data");
   hex(aidkey1, 17, "Key version and 16 byte AES key data");
   int             s = open(port, O_RDWR);
   if (s < 0)
      err(1, "Cannot open %s", port);
   {                            /* Terminal set up */
      struct termios  t;
      if (tcgetattr(s, &t))
         err(1, "Failed to get serial setting");
      cfmakeraw(&t);
      cfsetispeed(&t, 115200);
      cfsetospeed(&t, 115200);
      if (tcsetattr(s, TCSANOW, &t))
         err(1, "Failed to set serial");
   }

   j_t             j = j_create();


   const char     *e;           /* error */

   unsigned char   outputs = (gpio(red) | gpio(amber) | gpio(green));

   if ((e = pn532_init(s, outputs)))
      errx(1, "Cannot init PN532 on %s: %s", port, e);

   setled(s, led);

   /* Wait for card */
   unsigned char   nfcid[MAXNFCID] = {};
   unsigned char   ats[MAXATS] = {};
   int             cards = 0;
   while (!cards)
   {
      cards = pn532_Cards(s, nfcid, ats);
      if (cards < 0)
         errx(1, "Failed to get cards");
   }
   if (*nfcid)
      j_store_string(j, "id", j_base16a(*nfcid, nfcid + 1));
   if (*ats)
      j_store_string(j, "ats", j_base16a(*ats, ats + 1));

   df_t            d;
   if ((e = df_init(&d, &s, &pn532_dx)))
      errx(1, "Failed DF init: %s", e);
#define df(x,...) if((e=df_##x(&d,__VA_ARGS__)))errx(1,"Failed "#x": %s",e);

   unsigned char   binzero[17] = {};
   unsigned char  *currentkey = binmaster ? : binzero;

   unsigned char   ver[28];
   if (!(e = df_get_version(&d, ver)))
      j_store_string(j, "ver", j_base16a(sizeof(ver), ver));

   df(select_application, NULL);
   unsigned char   v;
   df(get_key_version, 0, &v);

   if (!binmaster || *binmaster != v || df_authenticate(&d, 0, binmaster + 1))
   {
      currentkey = binzero;
      df_authenticate(&d, 0, NULL);     /* try default */
   }
   if (!df_isauth(&d))
      warnx("Authentication failed, no further actions can be performed");
   else
   {

      {                         /* Get UID */
         unsigned char   uid[7];
         df(get_uid, uid);
         j_store_string(j, "uid", j_base16a(sizeof(uid), uid));
      }
      if (format)
      {
         df(format, *currentkey, currentkey + 1);
         if (binmaster && !setmaster)
         {
            df(change_key, 0x80, 0, currentkey + 1, NULL);      /* clear master key */
            currentkey = binzero;
            df(authenticate, 0, NULL);  /* re-authenticate */
         }
         j_store_boolean(j, "formatted", 1);
      }
      if (setmaster && currentkey == binzero)
      {
         if (!binmaster)
            fill_random(binmaster = malloc(17), 17);    /* new master */
         df(change_key, 0x80, *binmaster, currentkey + 1, binmaster + 1);
         currentkey = binmaster;
         df(authenticate, 0, binmaster + 1);
         df(change_key_settings, mastersetting);
         df(set_configuration, masterconfig);
         j_store_string(j, "master", j_base16a(17, binmaster));

      }
      if (createaid)
      {
         if (!binaid)
            errx(1, "Set --aid");
         df(create_application, binaid, aidsetting, aidkeys);
         if (!binaidkey0)
            fill_random(binaidkey0 = malloc(17), 17);   /* new key */
         if (!binaidkey1)
            fill_random(binaidkey1 = malloc(17), 17);   /* new key */
         df(change_key, 0, *binaidkey0, NULL, binaidkey0 + 1);
         j_store_boolean(j, "aidkey0", j_base16a(17, binaidkey0));
         if (aidkeys > 1)
         {
            df(authenticate, 1, NULL);  /* own key to change it */
            df(change_key, 1, *binaidkey1, NULL, binaidkey1 + 1);
            j_store_boolean(j, "aidkey1", j_base16a(17, binaidkey1));
         }
         df(authenticate, 0, binaidkey0 + 1);
         j_store_boolean(j, "aid", j_base16a(3, binaid));
      }
      /* TODO creating some files */
      /* TODO listing AIDs */
      /* TODO listing files in aid */
   }
   close(s);
   j_err(j_write_pretty(j, stdout));
   j_delete(&j);
   return 0;
}
