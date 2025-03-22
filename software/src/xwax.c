/*
 * Copyright (C) 2018 Mark Hills <mark@xwax.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h> /* mlockall() */

#include <unistd.h>         //Needed for I2C port
#include <fcntl.h>         //Needed for I2C port
#include <sys/ioctl.h>      //Needed for I2C port
#include <linux/i2c-dev.h> //Needed for I2C port
#include <time.h>
#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include "input/sc_midimap.h"
#include "input/controller.h"

#include "app/sc_input.h"
#include "app/sc_settings.h"

#include "player/dicer.h"
#include "player/track.h"
#include "thread/realtime.h"
#include "thread/thread.h"
#include "thread/rig.h"

#include "global/global.h"

#include "xwax.h"

struct mapping* maps = NULL;

void sig_handler( int signo )
{
   if ( signo == SIGINT )
   {
      printf("received SIGINT\n");
      exit(0);
   }
}

int main( int argc, char* argv[] )
{

   int rc = -1, priority;
   bool use_mlock;

   if ( signal(SIGINT, sig_handler) == SIG_ERR )
   {
      printf("\ncan't catch SIGINT\n");
      exit(1);
   }

   if ( setlocale(LC_ALL, "") == NULL )
   {
      fprintf(stderr, "Could not honour the local encoding\n");
      return -1;
   }
   if ( thread_global_init() == -1 )
   {
      return -1;
   }
   if ( rig_init() == -1 )
   {
      return -1;
   }
   rt_init(&g_rt);

   use_mlock = false;

   settings_load_user_configuration(&g_sc1000_settings, maps);

   sc1000_setup(&g_sc1000_engine, &g_sc1000_settings, &g_rt);
   sc1000_load_sample_folders(&g_sc1000_engine);

   rc = EXIT_FAILURE; /* until clean exit */

   // Start input processing thread
   start_sc_input_thread();

   // Start realtime stuff
   priority = 0;

   if ( rt_start(&g_rt, priority) == -1 )
   {
      return -1;
   }

   if ( use_mlock && mlockall(MCL_CURRENT) == -1 )
   {
      perror("mlockall");
      goto out_rt;
   }

   // Main loop

   fprintf(stderr, "WIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIP\n\n");

   if ( rig_main() == -1 )
   {
      goto out_interface;
   }

   // Exit

   rc = EXIT_SUCCESS;
   fprintf(stderr, "Exiting cleanly...\n");

   out_interface:
   out_rt:
   rt_stop(&g_rt);

   sc1000_clear(&g_sc1000_engine);

   rig_clear();
   thread_global_clear();

   if ( rc == EXIT_SUCCESS )
   {
      fprintf(stderr, "Done.\n");
   }

   return rc;
}
