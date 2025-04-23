/* EdFS -- An educational file system
 *
 * Copyright (C) 2019  Leiden University, The Netherlands.
 */

 #ifndef __EDFS_COMMON_H__
 #define __EDFS_COMMON_H__
 
 #include "edfs.h"
 
 #include <stdint.h>
 #include <stdbool.h>
 #include <unistd.h>
 
 
 /* Structure to use as handle to an opened image file. */
 typedef struct
 {
   int fd;
   const char *filename;
 
   edfs_super_block_t sb;
 } edfs_image_t;
 
 
 void           edfs_image_close           (edfs_image_t *img);
 edfs_image_t  *edfs_image_open            (const char   *filename,
                                            bool          read_super);
 
 
 
 /*
  * Inode-related routines
  */
 
 
 /* In-memory representation of an inode, we store inode number and
  * actual inode read from disk together in a structure.
  */
 typedef struct
 {
   edfs_inumber_t inumber;
   edfs_disk_inode_t inode;
 } edfs_inode_t;
 
 
 /* ------------------------------------------------------------------ */
 /*  Directory-iteration helper (added for Assignment 3, §4.1)         */
 /* ------------------------------------------------------------------ */
 
 typedef bool (*edfs_dir_iter_cb)(const edfs_dir_entry_t *entry,
                                  void                  *userdata);
 
 /* Iterate over all valid directory entries in @dir_inode.
  * Callback returns true -> stop early.
  * Returns 0 on success or a negative errno.                           */
 int edfs_scan_directory(edfs_image_t       *img,
                         const edfs_inode_t *dir_inode,
                         edfs_dir_iter_cb    cb,
                         void               *userdata);
 /* ------------------------------------------------------------------ */
 
 /* ------------------------------------------------------------- *
 *  Block-lookup helper (needed for edfuse_read)                 *
 * ------------------------------------------------------------- */

/* Translate a file offset to:
 *   – the disk block number that holds the data
 *   – the offset inside that block
 * Returns 0 on success, negative errno on error.                 */
 int edfs_block_for_offset(edfs_image_t       *img,
                           const edfs_inode_t *inode,
                           off_t               offset,
                           edfs_block_t       *block_out,
                           off_t              *inblock_off);

 int            edfs_read_inode            (edfs_image_t *img,
                                            edfs_inode_t *inode);
 int            edfs_read_root_inode       (edfs_image_t *img,
                                            edfs_inode_t *inode);
 int            edfs_write_inode           (edfs_image_t *img,
                                            edfs_inode_t *inode);
 int            edfs_clear_inode           (edfs_image_t *img,
                                            edfs_inode_t *inode);
 edfs_inumber_t edfs_find_free_inode       (edfs_image_t *img);
 int            edfs_new_inode             (edfs_image_t *img,
                                            edfs_inode_t *inode,
                                            edfs_inode_type_t type);
 /* ------------------------------------------------------------- *
 *  Block allocation helpers                                      *
 * ------------------------------------------------------------- */

/* Allocate one free disk block, mark it in the bitmap,
 * return 0-based block number in *block_out.
 * Returns 0 on success, negative errno on failure.               */
int edfs_alloc_block(edfs_image_t *img, edfs_block_t *block_out);

/* Mark @block as free again in the bitmap.                       */
int edfs_free_block(edfs_image_t *img, edfs_block_t block);

/* ------------------------------------------------------------- *
 *  Directory-entry helper                                         *
 * ------------------------------------------------------------- */

/* Insert a new entry (name + inumber) into the directory inode.
 * Allocates a new data block for the dir when current blocks are full.
 * Returns 0 on success or negative errno.                         */
int edfs_add_dir_entry(edfs_image_t       *img,
                       edfs_inode_t       *dir_inode,
                       const char         *name,
                       edfs_inumber_t      inumber);

/* ------------------------------------------------------------- *
 *  Block-ensure helper (needed for write / truncate)            *
 * ------------------------------------------------------------- */

/* Make sure data block #logical_idx exists for @inode.
 * Allocates data blocks (and indirect blocks) as needed and
 * writes the inode back to disk when it changes.
 * Returns 0 on success, negative errno on failure.               */
int edfs_ensure_block(edfs_image_t *img,
  edfs_inode_t *inode,          /* may be modified */
  uint32_t      logical_idx,
  edfs_block_t *block_out);

 #endif /* __EDFS_COMMON_H__ */
 