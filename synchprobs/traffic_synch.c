#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>
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
static struct lock *intersectionLock;
static struct array *directionset_array;
static struct semaphore *intersectionSem;

struct directionset {
    Direction ori;
    Direction des;
};

// return true if this is a right turn, return false otherwise
bool turning_right(Direction origin, Direction destination);
// return true if these two direction collid, return false otherwise
bool collid(struct directionset *direction1, struct directionset *direction2);
// return true is the direction in array will collid with given direction, return flase otherwise
bool collision_happen(struct array *directionset_array, struct directionset *direction);


bool turning_right(Direction origin, Direction destination) {
    if ((origin == north && destination == west) || (origin == south && destination == east) || (origin == east && destination == north) || (origin == west && destination == south)) {
        return true;
    } else {
        return false;
    }
}

bool collid(struct directionset *direction1, struct directionset *direction2) {
    if (direction1->ori == direction2->ori) {
        return false;
    } else if (direction1->ori == direction2->des && direction1->des == direction2->ori) {
        return false;
    } else if (direction1->des != direction2->des && (turning_right(direction1->ori, direction1->des) || turning_right(direction2->ori, direction2->des))) {
        return false;
    } else {
        return true;
    }
}

bool collision_happen(struct array *directionset_array, struct directionset *direction) {
    bool flag = false;
    for (unsigned i = 0; i < array_num(directionset_array); i++) {
        struct directionset *my_d = (struct directionset*)array_get(directionset_array, i);
        if (collid(direction, my_d)) {
            flag = true;
        }
    }
    return flag;
}
/*
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 *
 */
void intersection_sync_init(void) {
    intersectionLock = lock_create("intersectionLock");
    KASSERT(intersectionLock != NULL);
    intersectionSem = sem_create("intersectionSem", 1);
    KASSERT(intersectionSem != NULL);
    directionset_array = array_create();
    KASSERT(directionset_array != NULL);
    return;
}
/*
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void intersection_sync_cleanup(void) {
    KASSERT(intersectionLock != NULL);
    lock_destroy(intersectionLock);
    KASSERT(intersectionSem != NULL);
    sem_destroy(intersectionSem);
    array_destroy(directionset_array);
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
void intersection_before_entry(Direction origin, Direction destination) {
    struct directionset *my_direction = (struct directionset*)kmalloc(sizeof(struct directionset));
    KASSERT(my_direction != NULL);
    my_direction->ori = origin;
    my_direction->des = destination;
    lock_acquire(intersectionLock);
    while (collision_happen(directionset_array, my_direction)) {
        lock_release(intersectionLock);
        P(intersectionSem);
        lock_acquire(intersectionLock);
    }
    array_add(directionset_array, my_direction, NULL);
    lock_release(intersectionLock);
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
void intersection_after_exit(Direction origin, Direction destination) {
    lock_acquire(intersectionLock);
    for (unsigned i = 0; i < array_num(directionset_array); i++) {
        struct directionset *my_direction = (struct directionset*)array_get(directionset_array, i);
        if (my_direction->ori == origin && my_direction->des == destination) {
            array_remove(directionset_array, i);
            kfree(my_direction);
            break;
        }
    }
    V(intersectionSem);
    lock_release(intersectionLock);
}

