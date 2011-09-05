/* Double linked list macros derived from include/linux/list.h.
 *
 * Copyright (c) 2011 Michael Abbott, Diamond Light Source Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Contact:
 *      Dr. Michael Abbott,
 *      Diamond Light Source Ltd,
 *      Diamond House,
 *      Chilton,
 *      Didcot,
 *      Oxfordshire,
 *      OX11 0DE
 *      michael.abbott@diamond.ac.uk
 */


/* List head. */

struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name)  { &(name), &(name) }
#define LIST_HEAD(name)  struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}


/* Insert a new entry between two known consecutive entries. */
static inline void __list_add(
    struct list_head *new, struct list_head *prev, struct list_head *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

/* Insert a new entry after the specified head. */
static inline void list_add(struct list_head *new, struct list_head *head)
{
    __list_add(new, head, head->next);
}


/* Insert a new entry before the specified head. */
static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
    __list_add(new, head->prev, head);
}

/* Delete a list entry by making the prev/next entries
 * point to each other. */
static inline void __list_del(struct list_head * prev, struct list_head * next)
{
    next->prev = prev;
    prev->next = next;
}

/* Deletes entry from list. */
static inline void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
}


/* Casts a member of a structure out to the containing structure. */
#define container_of(ptr, type, member) \
    ( { \
        const typeof(((type *)0)->member) *__mptr = (ptr); \
        (type *)((void *)__mptr - offsetof(type, member)); \
    } )


/* Iterates over list head, setting pos to each entry in the list. */
#define list_for_each(pos, head) \
    for (struct list_head *pos = (head)->next; pos != (head); pos = pos->next)


/* list_for_each_entry - iterate over list of given type
 *  type:       type of the objects on the list
 *  member:     name of the list_struct within type.
 *  pos:        name of the list cursor.
 *  head:       head of the list.
 * Essentially the same as list_for_each(), except the cursor pos is pointed to
 * the containing structure at each iteration. */
#define list_for_each_entry(type, member, pos, head) \
    for (type *pos = container_of((head)->next, type, member); \
        &pos->member != (head); \
        pos = container_of(pos->member.next, type, member))
