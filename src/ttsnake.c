/* asciiplay.c - A very simple ascii movie player

   Copyright (c) 2021 - Monaco F. J. <monaco@usp.br>

   This file is part of TexTronSnake

   TexTronSnake is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <ncurses.h>
#include <config.h>

#include "utils.h"

/* Game defaults */

#define N_GAME_SCENES  3	/* Number of frames of the gamepay scnene. */

#define NCOLS 90		/* Number of columns of the scene. */
#define NROWS 40		/* Number of rows of the scene. */

#define BLANK ' '		/* Blank-screen character. */

#define BUFFSIZE 1024		/* Generic, auxilary buffer size. */
#define SCENE_DIR_INTRO "intro" /* Path to the intro animation scenes. */
#define SCENE_DIR_GAME  "game"	/* Path to the game animation scene. */

#define SNAKE_TAIL	 '.'	 /* Character to draw the snake tail. */
#define SNAKE_BODY       'x'     /* Character to draw the snake body. */
#define SNAKE_HEAD	 '0'	 /* Character to draw the snake head. */
#define ENERGY_BLOCK     '+'	 /* Character to draw the energy block. */

#define MAX_ENERGY_BLOCKS 3	/* Maximum number of energy blocks. */

#define MIN_GAME_DELAY 10200
#define MAX_GAME_DELAY 94000

/* Global variables.*/

struct timeval beginning,	/* Time when game started. */
  now,				/* Time now. */
  before,			/* Time in the last frame. */
  elapsed_last,			/* Elapsed time since last frame. */
  elapsed_total;		/* Elapsed time since game baginning. */


int movie_delay;		/* How long between move scenes scenes. */
int game_delay;			/* How long between game scenes. */
int go_on; 			/* Whether to continue or to exit main loop.*/
int player_lost;
int restart_game; /* Whether the user has pressed to restart the game or not */
int on_settings; /* Whether the user is currently changing settings */

int block_count; 		/*Number of energy blocks collected */
float score;     		/* Score: average blocks / time */

/* SIGINT handler. The variable go_on controls the main loop. */

void quit ()
{
  go_on=0;
}

/* The snake data structrue. */

typedef enum {up, right, left, down} direction_t;

typedef struct snake_st snake_t;

typedef struct pair_st
{
  int x, y;
} pair_t;

struct snake_st
{
  pair_t head;			 /* The snake's head. */
  int length;			 /* The snake length (including head). */
  pair_t *positions;	/* Position of each body part of the snake. */
  direction_t direction;	 /* Movement direction. */
};

snake_t snake;			/* The snake istance. */

/* Energy blocks. */

struct
{
  int x;			/* Coordinate x of the energy block. */
  int y;			/* Coordinate y of the energy block. */
} energy_block[MAX_ENERGY_BLOCKS]; /* Array of energy blocks. */

/* Load all scenes from dir into the scene vector.

   The scene vector is an array of nscenes matrixes of
   NROWS x NCOLS chars, containg the ascii image.

*/

/* All chars of one single scene. */

typedef char scene_t[NROWS][NCOLS];

/* Count how many scene files exist in the given directory and returns this number one.
   Complexity: O(log(n)) */

int countfiles(char* dir)
{
  FILE* file;
  char scenefile[1024];
  int k = 1, l;

  #define SFOPEN(file) \
  sprintf (scenefile, DATADIR "/" ALT_SHORT_NAME "/%s/scene-%07d.txt", dir, k); \
  file = fopen (scenefile, "r");

  /* Check if there are any file at all. */

  SFOPEN(file);

  if (!file)
    return 1;

  /* Double k until find a superior limit for files number. */

  do 
  {
    k *= 2;

    fclose(file);
    SFOPEN(file);
  }
  while (file);

  /* Binary search in the interval (k/2, k). */

  l = k / 8;
  for (k -= k / 4; l > 0; l /= 2)
  {
    SFOPEN(file);

    if (file)
    {
      k += l;
      fclose(file);
    }

    else
      k -= l;
  }

  /* Ensure that last step k is the last number for what there is a file. */

  SFOPEN(file);

  if (file)
    {
      return k;
      fclose(file);
    }

  else
    return k - 1;

  #undef SFOPEN
}

/* Read all the scenes in the 'dir' directory, save it in 'scene' and
   return the number of readed scenes. If zero is passed as nscenes,
   then calculate the actual number and allocate appropriate space in
   scene. */

int readscenes (char *dir, scene_t** scene, int nscenes)
{
  int i, j, k;
  FILE *file;
  char scenefile[1024], c, allocate = false;

  if (nscenes == 0)
  {
    nscenes = countfiles(dir);
    if (nscenes == 0)
    {
        endwin();
        sysfatal (nscenes == 0);
    }
    *scene = malloc(sizeof(**scene) * nscenes);
    allocate = true;
  }

  /* Read nscenes. */

  for (k=0; k<nscenes; k++)
  {

    /* Program always read scenes from the installed data path (DATADIR, e.g.
       /usr/share/<dir>. Therefore, if scenes are modified, they should be
       reinstalle (program won't read them from project tree.)  */
    sprintf (scenefile, DATADIR "/" ALT_SHORT_NAME "/%s/scene-%07d.txt", dir, k+1);

    /* Dont know if the line was for debug or not, commenting it
    printf ("Reading from %s\n", scenefile); */
    
    file = fopen (scenefile, "r");
    if (!file)
    {
      if (allocate)
        free(*scene);
      endwin();
      sysfatal (!file);
    }

    /* Write up and down borders and correct stream position of
       up border.*/

    for (j=0; j<NCOLS; j++)
    {
      (*scene)[k][0][j] = '-';
      (*scene)[k][NROWS-1][j] = '-';
    }

    fseek(file, sizeof(char) * NCOLS, SEEK_CUR);
    while (((c = fgetc(file)) != '\n') && (c != EOF));

    /* Iterate through NROWS. */

    for (i=1; i<NROWS-1; i++)
  	{

      /* Write left border and correct stream position */

      (*scene)[k][i][0] = '|';
      fseek(file, sizeof(char), SEEK_CUR);

      /* Read NCOLS columns from row i.*/

  	  for (j=1; j<NCOLS-1; j++)
      {

        /* Actual ascii text file may be smaller than NROWS x NCOLS.
           If we read something out of the 32-127 ascii range,
           consider a blank instead.*/

        c = (char) fgetc (file);
        (*scene)[k][i][j] = ((c>=' ') && (c<='~')) ? c : BLANK;
      }

      /* Write right border and correct stream position */

      (*scene)[k][i][NCOLS-1] = '|';
      fseek(file, sizeof(char), SEEK_CUR);


      /* Discard the rest of the line (if longer than NCOLS). */

  	  while (((c = fgetc(file)) != '\n') && (c != EOF));

  	}

  fclose (file);

  }

  return k;
}


/* Draw a the given scene on the screen. Currently, this iterates through the
   scene matrix outputig each caracter by means of indivudal puchar calls. One
   may want to try a different approach which favour performance. For instance,
   issuing a single 'write' call for each line. Would this yield any significant
   performance improvement? */

void draw (scene_t* scene, int number)
{
  int i, j;
  for (i=0; i<NROWS; i++)
    {
      for (j=0; j<NCOLS; j++)
	{
	  putchar (scene[number][i][j]);
	}
      putchar ('\n');
      putchar ('\r');
    }
}

#define BLOCK_INACTIVE -1

/* Draw scene indexed by number, get some statics and repeat.
   If meny is true, draw the game controls.*/
void showscene (scene_t* scene, int number, int menu)
{
  double fps;
  int i;

  /* Draw the scene. */

  draw (scene, number);

  memcpy (&before, &now, sizeof (struct timeval));
  gettimeofday (&now, NULL);

  if(!player_lost) {
    timeval_subtract (&elapsed_last, &now, &before);

    timeval_subtract (&elapsed_total, &now, &beginning);
  }

  /* Displays active energy blocks */

  for (i=0; i<MAX_ENERGY_BLOCKS; i++)
    if(energy_block[i].x != BLOCK_INACTIVE) scene[number][energy_block[i].y][energy_block[i].x] = ENERGY_BLOCK;


  fps = 1 / (elapsed_last.tv_sec + (elapsed_last.tv_usec * 1E-6));
  
  /*Calculate score based on time*/
  if (elapsed_total.tv_sec != 0)
  {
    score = (block_count / (float) (elapsed_total.tv_sec));
  }

  if (menu)
    {
      printf ("Elapsed: %5ds, fps=%5.2f\r\n", /* CR-LF because of ncurses. */
	      (int) elapsed_total.tv_sec, fps);
      /*Add to the menu score and blocks collected */	  
      printf ("Score: %.2f\r\n", score);
      printf ("Blocks: %d\r\n", block_count);  
	  
      printf ("Controls: q: quit | r: restart | WASD: move the snake | +/-: change game speed\r\n");
      printf ("          h: help & settings\r\n");
    }
}


/*This function generates a new set of energy blocks.
  there are checks to prevent it from spawning blocks
  on top of the borders and on top of the snake. It is called
  every MAX_ENERGY_BLOCKS-th block eaten*/
void more_snacks(){
   /* Generate energy blocks away from the borders and the snake */
 	int i = 0, j = 0, isValid;
	/*We generate a new block and check it against every piece of the snake.
	 if the block is on top of the snake, then we generate a new position
	 for it. Otherwise, we go to the next block and do it again.*/
    	do
  	{
  	  isValid = 1;
	  energy_block[i].x = (rand() % (NCOLS - 2)) + 1;
  	  energy_block[i].y = (rand() % (NROWS - 2)) + 1;
	  for(j = 0; j < snake.length;j++){
		  if(energy_block[i].x == snake.positions[j].x && energy_block[i].y == snake.positions[j].y){
			  isValid = 0;
			  break;
		  }
	  }
	  if(isValid == 1){
		  i++;
	  }

  	}while(i < MAX_ENERGY_BLOCKS);
        
	return;
}
  /* Instantiate the snake and a set of energy blocks. */

/* Put above the showscene function so I could use it to display active blocks on current scene */
/* #define BLOCK_INACTIVE -1 */

void init_game (scene_t* scene)
{
  int i;
	
  srand(time(NULL));
  /*Set initial score and blocks collected 0 */
  score = 0;
  block_count = 0;
	
  snake.head.x = 0;
  snake.head.y = 0;
  snake.direction = right;
  snake.length = 7;

	const pair_t initialPosition[] = {
		{10, 8},
		{11, 8},
		{12, 8},
		{13, 8},
		{14, 8},
		{14, 9},
		{14, 10}
	};

  /* Initialize position of the snake, from tail to head. */
	snake.positions = (pair_t *) malloc(snake.length * sizeof(pair_t));
	for(i = 0; i < snake.length; i++){
		snake.positions[i].x = initialPosition[i].x;
		snake.positions[i].y = initialPosition[i].y;

		scene[0][initialPosition[i].y][initialPosition[i].x] = SNAKE_BODY;
	}

   /* Generate energy blocks away from the borders */
  for (i=0; i<MAX_ENERGY_BLOCKS; i++)
  {
    energy_block[i].x = (rand() % (NCOLS - 2)) + 1 ;
    energy_block[i].y = (rand() % (NROWS - 2)) + 1;
  }

}

/* This function increases the snake's size by one.
   It adds the new piece of the snake's body to the
   first position of the vector, i.e., the new piece
   becomes the snake's tail. However, since this function
   is called after the snake has moved, the tail isn't 
   erased from the screen, and the visual effect 
   should be as if the new piece was added to the 
   middle of the body, even though technically the new 
   piece is added to the end of the body.
 */

void snake_snack(int tail_x, int tail_y)
{	
	pair_t *auxVector;
	int i, auxCounter;

	auxCounter = snake.length; /*Save the current length of the snake*/
	snake.length++;/*Increase the length of the snake*/

	auxVector = (pair_t *) malloc(auxCounter * sizeof(pair_t));/*allocate enough space*/
	memmove(auxVector, snake.positions, auxCounter * sizeof(pair_t));/*save all snake positions*/

	snake.positions =(pair_t*)realloc(snake.positions, sizeof(pair_t) * snake.length);/*Increase the size of the positions vector*/
	snake.positions[0].y = tail_y;/*Add the new piece to the snake's body*/
	snake.positions[0].x = tail_x;
	
	for(i = 0; i <  auxCounter; i++){/*Repopulate the snake.positions vector*/
		snake.positions[i + 1].x = auxVector[i].x;
		snake.positions[i + 1].y = auxVector[i].y;
	}

	free(auxVector);
	return;
}	


/* This function advances the game. It computes the next state
   and updates the scene vector. This is Tron's game logic. */

void advance (scene_t* scene)
{
	pair_t head, tail, last1_tail, last2_tail, body;
	/* Setting the body position. */
	body = snake.positions[snake.length - 1];
	/* Setting the head position. */
	head = snake.positions[snake.length - 1];
	/* Setting the tail position. */
	last1_tail = snake.positions[1];
	last2_tail = snake.positions[2];
	tail = snake.positions[0];

	int i, flag = 0;

	/* Calculate next position of the head. */
	switch(snake.direction){
		case up:
			head.y -= 1;
			break;
		case right:
			head.x += 1;
			break;
		case left:
			head.x -= 1;
			break;
		case down:
			head.y += 1;
			break;
	}

	/*When the head position is the same as the energy block*/
	for(i = 0; i < MAX_ENERGY_BLOCKS; i++)
	{
		if(head.x == energy_block[i].x && head.y == energy_block[i].y)
		{
			block_count += 1;
			flag = 1;
			energy_block[i].x = BLOCK_INACTIVE;
			if(block_count%MAX_ENERGY_BLOCKS == 0){
			       	more_snacks();
			}
		}
	}
	

  /* Check if head collided with border or itself */
  if(   head.x <= 0 || head.x >= NCOLS - 1
     || head.y <= 0 || head.y >= NROWS - 1
     || scene[0][head.y][head.x] == SNAKE_BODY)
  {
      player_lost = 1;
      return;
  }

	/* Advance snake in one step */
	memmove(snake.positions, snake.positions + 1, sizeof(pair_t) * (snake.length-1));
	snake.positions[snake.length - 1] = head;

	/* Erase old position of the tail or add new piece to the snake */
	if(flag == 0)
	{	
		scene[0][tail.y][tail.x] = ' ';
	}else{
		flag = 0;
		snake_snack(tail.x, tail.y);
	}

	/* Draw new two position of the tail */
	scene[0][last1_tail.y][last1_tail.x] = SNAKE_TAIL;
	scene[0][last2_tail.y][last2_tail.x] = SNAKE_TAIL;
	/* Draw new position of the body */
	scene[0][body.y][body.x] = SNAKE_BODY;
	/* Draw new position of the head */
	scene[0][head.y][head.x] = SNAKE_HEAD;
}

/* This function plays the game introduction animation. */

void playmovie (scene_t* scene, int nscenes)
{

  int k;
  struct timespec how_long;
  how_long.tv_sec = 0;

  for (k=0; (k < nscenes) && (go_on); k++)
    {
      clear ();			               /* Clear screen.    */
      refresh ();			       /* Refresh screen.  */
      showscene (scene, k, 0);                 /* Show k-th scene .*/
      how_long.tv_nsec = (movie_delay) * 1e3;  /* Compute delay. */
      nanosleep (&how_long, NULL);	       /* Apply delay. */
    }
}


/* This function implements the gameplay loop. */

void playgame (scene_t* scene)
{

  struct timespec how_long;
  how_long.tv_sec = 0;

  /* User may change delay (game speedy) asynchronously. */

  while (go_on)
    {
      clear ();                               /* Clear screen. */
      refresh ();			      /* Refresh screen. */

      if(!on_settings)
        advance (scene);		               /* Advance game.*/

      if(player_lost){
        /* Write score on the scene */
        char buffer[128];
        sprintf(buffer, "%.2f", score);
        memcpy(&scene[1][27][30], buffer, strlen(buffer));
      }

      if(restart_game) {
        /* Reset variables as at the beginning of the game */
        go_on=1;
        player_lost=0;
        restart_game=0;
        gettimeofday (&beginning, NULL);

        init_game (scene);
        readscenes (SCENE_DIR_GAME, &scene, N_GAME_SCENES);
      }

      showscene (scene, /* Show k-th scene. */
        player_lost ? 1 : on_settings ? 2 : 0,
        on_settings ? 0 : 1);

      how_long.tv_nsec = (game_delay) * 1e3;  /* Compute delay. */
      nanosleep (&how_long, NULL);	      /* Apply delay. */
    }

}


/* Process user input.
   This function runs in a separate thread. */

void * userinput ()
{
  int c;
  while (1)
  {
    c = getchar();

    if(on_settings)
    {
      switch(c)
      {
        case 'q':
          on_settings=0;
          restart_game=1;
        break;
        default:
        break;
      }
    } else {
      switch (c)
      {
      case '+':			/* Increase FPS. */
        if(game_delay * (0.9) > MIN_GAME_DELAY)
          game_delay *= (0.9);
      break;
      case '-':			/* Decrease FPS. */
        if(game_delay * (1.1) < MAX_GAME_DELAY)
          game_delay *= (1.1) ;
      break;
      case 'q':
        kill (0, SIGINT);	/* Quit. */
      break;
      case 'r':
        restart_game = 1;	/* Restart game. */
      break;
      case 'w':
        if(snake.direction != down)
          snake.direction = up;
      break;
      case 'a':
        if(snake.direction != right)
          snake.direction = left;
      break;
      case 's':
        if(snake.direction != up)
          snake.direction = down;
      break;
      case 'd':
        if(snake.direction != left)
          snake.direction = right;
      break;
      case 'h':
        on_settings = 1; /* Begin settings */
        break;
      default:
      break;
      }
    }
  }
}


/* The main function. */

int main ()
{
  struct sigaction act;
  int rs;
  int nscenes;
  pthread_t pthread;
  scene_t* intro_scene;
  scene_t* game_scene;

  game_scene = (scene_t *) malloc(sizeof(*game_scene) * N_GAME_SCENES);
  if(!game_scene){
    endwin();
    sysfatal(!game_scene);
  }

  /* Handle SIGNINT (loop control flag). */

  sigaction(SIGINT, NULL, &act);
  act.sa_handler = quit;
  sigaction(SIGINT, &act, NULL);

  /* Ncurses initialization. */

  initscr();
  noecho();
  curs_set(FALSE);
  cbreak();

  /* Default values. */

  movie_delay = 1E5 / 4;	  /* Movie frame duration in usec (40usec) */
  game_delay  = 1E6 / 4;	  /* Game frame duration in usec (4usec) */


  /* Handle game controls in a different thread. */

  rs = pthread_create (&pthread, NULL, userinput, NULL);
  sysfatal (rs);


  /* Play intro. */

  nscenes = readscenes (SCENE_DIR_INTRO, &intro_scene, 0);

  go_on=1;			/* User may skip intro (q). */

  playmovie (intro_scene, nscenes);

  /* Play game. */

  readscenes (SCENE_DIR_GAME, &game_scene, N_GAME_SCENES);

  go_on=1;
  player_lost=0;
  restart_game=0;
  on_settings=0;
  gettimeofday (&beginning, NULL);

  init_game (game_scene);
  playgame (game_scene);

  endwin();
  free(intro_scene);
  free(game_scene);

  return EXIT_SUCCESS;
}
