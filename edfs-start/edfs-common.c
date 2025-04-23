/* EdFS -- An educational file system
 *
 * Copyright (C) 2019  Leiden University, The Netherlands.
 */

#include "edfs-common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * EdFS image management
 */

void
edfs_image_close(edfs_image_t *img)
{
  if (!img)
    return;

  if (img->fd >= 0)
    close(img->fd);

  free(img);
}

/* Read and verify super block. */
static bool
edfs_read_super(edfs_image_t *img)
{
  if (pread(img->fd, &img->sb, sizeof(edfs_super_block_t), EDFS_SUPER_BLOCK_OFFSET) < 0)
    {
      fprintf(stderr, "error: file '%s': %s\n",
              img->filename, strerror(errno));
      return false;
    }

  if (img->sb.magic != EDFS_MAGIC)
    {
      fprintf(stderr, "error: file '%s': EdFS magic number mismatch.\n",
              img->filename);
      return false;
    }

  /* Simple sanity check of size of file system image. */
  struct stat buf;

  if (fstat(img->fd, &buf) < 0)
    {
      fprintf(stderr, "error: file '%s': stat failed? (%s)\n",
              img->filename, strerror(errno));
      return false;
    }

  if (buf.st_size < edfs_get_size(&img->sb))
    {
      fprintf(stderr, "error: file '%s': file system size larger than image size.\n",
              img->filename);
      return false;
    }

  /* FIXME: implement more sanity checks? */

  return true;
}

edfs_image_t *
edfs_image_open(const char *filename, bool read_super)
{
  edfs_image_t *img = malloc(sizeof(edfs_image_t));

  img->filename = filename;
  img->fd = open(img->filename, O_RDWR);
  if (img->fd < 0)
    {
      fprintf(stderr, "error: could not open file '%s': %s\n",
              img->filename, strerror(errno));
      edfs_image_close(img);
      return NULL;
    }

  /* Load super block into memory. */
  if (read_super && !edfs_read_super(img))
    {
      edfs_image_close(img);
      return NULL;
    }

  return img;
}


/*
 * Inode-related routines
 */

/* Read inode from disk, inode->inumber must be set to the inode number
 * to be read from disk.
 */
int
edfs_read_inode(edfs_image_t *img,
                edfs_inode_t *inode)
{
  if (inode->inumber >= img->sb.inode_table_n_inodes)
    return -ENOENT;

  off_t offset = edfs_get_inode_offset(&img->sb, inode->inumber);
  return pread(img->fd, &inode->inode, sizeof(edfs_disk_inode_t), offset);
}

/* Reads the root inode from disk. @inode must point to a valid
 * inode structure.
 */
int
edfs_read_root_inode(edfs_image_t *img,
                     edfs_inode_t *inode)
{
  inode->inumber = img->sb.root_inumber;
  return edfs_read_inode(img, inode);
}

/* Writes @inode to disk, inode->inumber must be set to a valid
 * inode number to which the inode will be written.
 */
int
edfs_write_inode(edfs_image_t *img, edfs_inode_t *inode)
{
  if (inode->inumber >= img->sb.inode_table_n_inodes)
    return -ENOENT;

  off_t offset = edfs_get_inode_offset(&img->sb, inode->inumber);
  return pwrite(img->fd, &inode->inode, sizeof(edfs_disk_inode_t), offset);
}

/* Clears the specified inode on disk, based on inode->inumber.
 */
int
edfs_clear_inode(edfs_image_t *img, edfs_inode_t *inode)
{
  if (inode->inumber >= img->sb.inode_table_n_inodes)
    return -ENOENT;

  off_t offset = edfs_get_inode_offset(&img->sb, inode->inumber);

  edfs_disk_inode_t disk_inode;
  memset(&disk_inode, 0, sizeof(edfs_disk_inode_t));
  return pwrite(img->fd, &disk_inode, sizeof(edfs_disk_inode_t), offset);
}

/* Finds a free inode and returns the inumber. NOTE: this does NOT
 * allocate the inode. Only after a valid inode has been written
 * to this inumber, this inode is allocated in the table.
 */
edfs_inumber_t
edfs_find_free_inode(edfs_image_t *img)
{
  edfs_inode_t inode = { .inumber = 1 };

  while (inode.inumber < img->sb.inode_table_n_inodes)
    {
      if (edfs_read_inode(img, &inode) > 0 &&
          inode.inode.type == EDFS_INODE_TYPE_FREE)
        return inode.inumber;

      inode.inumber++;
    }

  return 0;
}

/* Create a new inode. Searches for a free inode in the inode table (returns
 * -ENOSPC if the inode table is full). @inode is initialized accordingly.
 */
int
edfs_new_inode(edfs_image_t *img,
               edfs_inode_t *inode,
               edfs_inode_type_t type)
{
  edfs_inumber_t inumber;

  inumber = edfs_find_free_inode(img);
  if (inumber == 0)
    return -ENOSPC;

  memset(inode, 0, sizeof(edfs_inode_t));
  inode->inumber = inumber;
  inode->inode.type = type;

  return 0;
}

/* ===================================================================== *
 *  edfs_scan_directory  –  generic directory walker                     *
 *  Added for Assignment 3 (§4.1): we need it in find-inode, readdir, …  *
 * ===================================================================== */
int
edfs_scan_directory(edfs_image_t       *img,
                    const edfs_inode_t *dir,
                    edfs_dir_iter_cb    cb,
                    void               *userdata)
{
  if (!edfs_disk_inode_is_directory(&dir->inode))
    return -ENOTDIR;

  const uint16_t block_size        = img->sb.block_size;
  const size_t   entries_per_block = edfs_get_n_dir_entries_per_block(&img->sb);

  edfs_dir_entry_t *buffer = malloc(block_size);
  if (!buffer)
    return -ENOMEM;

  for (int i = 0; i < EDFS_INODE_N_BLOCKS; ++i)
    {
      edfs_block_t blk = dir->inode.blocks[i];
      if (blk == EDFS_BLOCK_INVALID)
        continue;                       /* block not allocated */

      off_t off = edfs_get_block_offset(&img->sb, blk);
      if (pread(img->fd, buffer, block_size, off) != block_size)
        { free(buffer); return -EIO; }

      for (size_t j = 0; j < entries_per_block; ++j)
        {
          edfs_dir_entry_t *de = &buffer[j];
          if (edfs_dir_entry_is_empty(de))
            continue;

          if (cb(de, userdata))         /* stop early if cb returns true */
            { free(buffer); return 0; }
        }
    }

  free(buffer);
  return 0;
}

/* ================================================================= *
 *  edfs_block_for_offset                                            *
 * ================================================================= */
int
edfs_block_for_offset(edfs_image_t       *img,
                      const edfs_inode_t *inode,
                      off_t               offset,
                      edfs_block_t       *block_out,
                      off_t              *inblock_off)
{
  /* bounds check */
  if (offset < 0 || (uint32_t)offset >= inode->inode.size)
    return -EINVAL;

  uint16_t bs   = img->sb.block_size;
  uint32_t idx  = offset / bs;          /* which data block within the file */
  *inblock_off  = offset % bs;

  if (!edfs_disk_inode_has_indirect(&inode->inode))
    {
      /* direct blocks only (directories and small files) */
      if (idx >= EDFS_INODE_N_BLOCKS)
        return -EIO;

      edfs_block_t blk = inode->inode.blocks[idx];
      if (blk == EDFS_BLOCK_INVALID)
        return -EIO;

      *block_out = blk;
      return 0;
    }

  /* -------- indirect case -------- */
  uint32_t per_indirect = edfs_get_n_blocks_per_indirect_block(&img->sb);
  uint32_t ind_slot     = idx / per_indirect;
  uint32_t ind_index    = idx % per_indirect;

  if (ind_slot >= EDFS_INODE_N_BLOCKS)
    return -EIO;

  edfs_block_t ind_blk = inode->inode.blocks[ind_slot];
  if (ind_blk == EDFS_BLOCK_INVALID)
    return -EIO;

  /* read the indirect block (array of edfs_block_t) */
  size_t buf_bytes = img->sb.block_size;
  edfs_block_t *array = malloc(buf_bytes);
  if (!array)
    return -ENOMEM;

  if (pread(img->fd, array, buf_bytes,
            edfs_get_block_offset(&img->sb, ind_blk)) != (ssize_t)buf_bytes)
    { free(array); return -EIO; }

  edfs_block_t data_blk = array[ind_index];
  free(array);

  if (data_blk == EDFS_BLOCK_INVALID)
    return -EIO;

  *block_out = data_blk;
  return 0;
}

/* ================================================================= *
 *  Bitmap helpers: edfs_alloc_block / edfs_free_block               *
 * ================================================================= */
static int
bitmap_set(edfs_image_t *img, edfs_block_t blk, bool value)
{
  uint32_t bit  = blk;
  uint32_t byte = bit / 8;
  uint8_t  mask = 1u << (bit % 8);

  off_t off = img->sb.bitmap_start + byte;
  uint8_t data;

  if (pread(img->fd, &data, 1, off) != 1)
    return -EIO;

  if (value)
    { if (data & mask) return -EEXIST;  data |= mask; }
  else
    { if (!(data & mask)) return -ENOENT; data &= ~mask; }

  if (pwrite(img->fd, &data, 1, off) != 1)
    return -EIO;

  return 0;
}

int
edfs_alloc_block(edfs_image_t *img, edfs_block_t *block_out)
{
  uint32_t nbytes = img->sb.bitmap_size;
  uint8_t *bmp = malloc(nbytes);
  if (!bmp) return -ENOMEM;

  if (pread(img->fd, bmp, nbytes, img->sb.bitmap_start) != (ssize_t)nbytes)
    { free(bmp); return -EIO; }

  for (uint32_t byte = 0; byte < nbytes; ++byte)
    {
      if (bmp[byte] == 0xFF) continue;          /* all 8 blocks used */
      for (int bit = 0; bit < 8; ++bit)
        {
          if (!(bmp[byte] & (1u << bit)))
            {
              edfs_block_t blk = byte * 8 + bit;
              free(bmp);
              int rc = bitmap_set(img, blk, true);
              if (rc == 0) *block_out = blk;
              return rc;
            }
        }
    }
  free(bmp);
  return -ENOSPC;
}

int
edfs_free_block(edfs_image_t *img, edfs_block_t block)
{
  return bitmap_set(img, block, false);
}

/* ================================================================= *
 *  edfs_add_dir_entry                                               *
 * ================================================================= */
int
edfs_add_dir_entry(edfs_image_t *img,
                   edfs_inode_t *dir,
                   const char   *name,
                   edfs_inumber_t inumber)
{
  if (!edfs_disk_inode_is_directory(&dir->inode))
    return -ENOTDIR;

  if (strlen(name) >= EDFS_FILENAME_SIZE)
    return -EINVAL;

  const uint16_t bs = img->sb.block_size;
  const int ents_per_blk = edfs_get_n_dir_entries_per_block(&img->sb);
  edfs_dir_entry_t *buf = malloc(bs);
  if (!buf) return -ENOMEM;

  /* try to find free slot in existing blocks */
  for (int i = 0; i < EDFS_INODE_N_BLOCKS; ++i)
    {
      if (dir->inode.blocks[i] == EDFS_BLOCK_INVALID)
        continue;

      off_t off = edfs_get_block_offset(&img->sb, dir->inode.blocks[i]);
      if (pread(img->fd, buf, bs, off) != bs)
        { free(buf); return -EIO; }

      for (int j = 0; j < ents_per_blk; ++j)
        {
          if (edfs_dir_entry_is_empty(&buf[j]))
            {
              buf[j].inumber = inumber;
              strncpy(buf[j].filename, name, EDFS_FILENAME_SIZE);
              if (pwrite(img->fd, buf, bs, off) != bs)
                { free(buf); return -EIO; }
              free(buf);
              return 0;
            }
        }
    }

  /* need a new block */
  int slot = -1;
  for (int i = 0; i < EDFS_INODE_N_BLOCKS; ++i)
    if (dir->inode.blocks[i] == EDFS_BLOCK_INVALID)
      { slot = i; break; }

  if (slot < 0)
    { free(buf); return -ENOSPC; }          /* directory full */

  edfs_block_t newblk;
  int rc = edfs_alloc_block(img, &newblk);
  if (rc < 0) { free(buf); return rc; }

  /* zero the new block first */
  memset(buf, 0, bs);
  buf[0].inumber = inumber;
  strncpy(buf[0].filename, name, EDFS_FILENAME_SIZE);

  if (pwrite(img->fd, buf, bs, edfs_get_block_offset(&img->sb, newblk)) != bs)
    { free(buf); return -EIO; }

  dir->inode.blocks[slot] = newblk;
  /* update inode on disk */
  rc = edfs_write_inode(img, dir);
  free(buf);
  return rc;
}

/* ================================================================= *
 *  edfs_ensure_block                                                *
 * ================================================================= */
int
edfs_ensure_block(edfs_image_t *img,
                  edfs_inode_t *inode,
                  uint32_t      idx,
                  edfs_block_t *block_out)
{
  const uint32_t bs      = img->sb.block_size;
  const uint32_t per_ind = edfs_get_n_blocks_per_indirect_block(&img->sb);

  /* --- direct blocks case --------------------------------------- */
  if (!edfs_disk_inode_has_indirect(&inode->inode))
    {
      if (idx >= EDFS_INODE_N_BLOCKS)
        {
          /* need to convert to indirect */
          edfs_block_t ind_blk;
          int rc = edfs_alloc_block(img, &ind_blk);
          if (rc < 0) return rc;

          /* zero-initialise indirect block */
          size_t bytes = bs;
          void *zero = calloc(1, bytes);
          pwrite(img->fd, zero, bytes,
                 edfs_get_block_offset(&img->sb, ind_blk));
          free(zero);

          /* copy old direct pointers into indirect block */
          edfs_block_t copy[EDFS_INODE_N_BLOCKS];
          memcpy(copy, inode->inode.blocks,
                 sizeof(edfs_block_t)*EDFS_INODE_N_BLOCKS);
          pwrite(img->fd, copy,
                 sizeof(copy),
                 edfs_get_block_offset(&img->sb, ind_blk));

          memset(inode->inode.blocks, 0,
                 sizeof(edfs_block_t)*EDFS_INODE_N_BLOCKS);
          inode->inode.blocks[0] = ind_blk;
          inode->inode.type |= EDFS_INODE_TYPE_INDIRECT;
          edfs_write_inode(img, inode);
        }
      else
        {
          /* still in direct range */
          if (inode->inode.blocks[idx] == EDFS_BLOCK_INVALID)
            {
              int rc = edfs_alloc_block(img, &inode->inode.blocks[idx]);
              if (rc < 0) return rc;
              edfs_write_inode(img, inode);
            }
          *block_out = inode->inode.blocks[idx];
          return 0;
        }
    }

  /* --- indirect case -------------------------------------------- */
  uint32_t slot   = idx / per_ind;
  uint32_t offset = idx % per_ind;
  if (slot >= EDFS_INODE_N_BLOCKS)
    return -EFBIG;

  /* ensure indirect block present */
  if (inode->inode.blocks[slot] == EDFS_BLOCK_INVALID)
    {
      int rc = edfs_alloc_block(img, &inode->inode.blocks[slot]);
      if (rc < 0) return rc;

      void *zero = calloc(1, bs);
      pwrite(img->fd, zero, bs,
             edfs_get_block_offset(&img->sb, inode->inode.blocks[slot]));
      free(zero);
      edfs_write_inode(img, inode);
    }

  /* load indirect block */
  edfs_block_t *array = malloc(bs);
  if (!array) return -ENOMEM;
  pread(img->fd, array, bs,
        edfs_get_block_offset(&img->sb, inode->inode.blocks[slot]));

  if (array[offset] == EDFS_BLOCK_INVALID)
    {
      int rc = edfs_alloc_block(img, &array[offset]);
      if (rc < 0) { free(array); return rc; }

      pwrite(img->fd, array, bs,
             edfs_get_block_offset(&img->sb, inode->inode.blocks[slot]));
    }

  *block_out = array[offset];
  free(array);
  return 0;
}
