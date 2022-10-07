#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/* 
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
// static struct semaphore *intersectionSem;

// The direction that is allowed to go
static Direction currentLight;

// Number of vehicles waiting from each direction: north, east, south, west
// Do I need to declare this as volatile?
static int numWaiting[4];
// static volatile int numInIntersection[4];
static int numInIntersection;

static struct lock *mutex;

static struct cv *lightOpen[4];

/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */

void
intersection_change_light(void)
{
    int i, j;
    for (i = 0; i < 4; i += 1) {
      j = (currentLight + i + 1) % 4;
      if (numWaiting[j] > 0 && j != currentLight) {
        currentLight = j;
        // kprintf("light changed to %d\n", currentLight);
        cv_broadcast(lightOpen[currentLight], mutex);
        break;
      }
    }
}

void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  currentLight = random() % 4;
  // kprintf("light changed to %d\n", currentLight);

  numInIntersection = 0;
  unsigned int i;

  
  for (i = 0; i < 4; i += 1) {
    numWaiting[i] = 0;
  }


  mutex = lock_create("mutex");
  if (mutex == NULL) {
    panic("could not create mutex lock for cv");
  }

  for (i = 0; i < 4; i += 1) {
    lightOpen[i] = cv_create("lightOpen");
    if (lightOpen[i] == NULL) {
      panic("could not create cv for directions");
    }
  }
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  // KASSERT(intersectionSem != NULL);
  // sem_destroy(intersectionSem);
  KASSERT(mutex != NULL);
  lock_destroy(mutex);

  unsigned int i;
  for (i = 0; i < 4; i += 1) {
    KASSERT(lightOpen[i] != NULL);
    cv_destroy(lightOpen[i]);
  }
}

/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */

  /*
   * Default: only allows 1 vehicle to pass through the intersection at a time.
   * Sem is created with an initial value of 1, meaning the vehicle "quota" for the intersection
   * P(): if value > 0, decrement
   * if value == 0, wait until value > 0 and then decrement
   * After the P(), vehicle enters the intersection
   */

  // KASSERT(intersectionSem != NULL);
  // P(intersectionSem);

  KASSERT(mutex != NULL);

  lock_acquire(mutex);
  numWaiting[origin] += 1;

  if (numInIntersection == 0 && numWaiting[currentLight] == 0) {
    intersection_change_light();
  }

  // kprintf("%d %d introduced, %d in intersection, %d, %d, %d, %d\n", origin, destination, numInIntersection, numWaiting[0], numWaiting[1], numWaiting[2], numWaiting[3]);

  // Wait until the current allowed direction is the origin
  while (origin != currentLight) {
    KASSERT(lightOpen[origin] != NULL);
    cv_wait(lightOpen[origin], mutex);
  }

  // Vehicle enters the intersection
  numWaiting[origin] -= 1;
  numInIntersection += 1;
  // kprintf("%d %d entered the intersection, %d in intersection, %d, %d, %d, %d\n", origin, destination, numInIntersection, numWaiting[0], numWaiting[1], numWaiting[2], numWaiting[3]);

  lock_release(mutex);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  (void)origin;  /* avoid compiler complaint about unused parameter */
  (void)destination; /* avoid compiler complaint about unused parameter */

  KASSERT(mutex != NULL);

  lock_acquire(mutex);
  // Vehicle leaves the intersection
  numInIntersection -= 1;
  // kprintf("%d %d left the intersection, %d in intersection, %d, %d, %d, %d\n", origin, destination, numInIntersection, numWaiting[0], numWaiting[1], numWaiting[2], numWaiting[3]);
  if (numInIntersection == 0 && numWaiting[currentLight] == 0) {
    intersection_change_light();
  }
  lock_release(mutex);
}
