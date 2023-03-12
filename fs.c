// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"
#define min(i, j) (((i) < (j)) ? (i) : (j))

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) { 
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0; 
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {
  i32 inum = bfsFdToInum(fd); // inum
  i32 size = bfsGetSize(inum); // size of file
  i32 cursor = bfsTell(fd); // cursor starting position
  i32 currFBN = cursor / BYTESPERBLOCK; // starting and current block to read
  if (cursor + numb > size) numb = size - cursor; // if set to read past file, read to EOF
  i32 lastFBN = (cursor + numb) / BYTESPERBLOCK; // last block to be read

  i8 bioBuff[BYTESPERBLOCK];
  i32 currCursor = cursor; // the cursor current cursor position, updated on each block read
  while (currFBN <= lastFBN) { // while there is a block to read
    bfsRead(inum, currFBN, bioBuff); // read the whole current block
    i32 start = currCursor - (currFBN * BYTESPERBLOCK); // start == current cursor position - starting point of current block
    i32 read = min(numb - (currCursor - cursor), ((currFBN + 1) * BYTESPERBLOCK) - currCursor); // read == min(remaining bytes to read, amount that can be read in block from start)
    memcpy(buf + (currCursor - cursor), bioBuff + start, read); // write into buf the contents of bioBuff from startBuff to endBuff
    currCursor += read; // move cursor by the amount of bytes written
    currFBN++; // move to next block
  }
  fsSeek(fd, numb, SEEK_CUR); // set cursor
  return numb;
}


// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);
 
  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);
  
  switch(whence) {
    case SEEK_SET:
      g_oft[ofte].curs = offset;
      break;
    case SEEK_CUR:
      g_oft[ofte].curs += offset;
      break;
    case SEEK_END: {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
      }
    default:
        FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}



// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {
  i32 inum = bfsFdToInum(fd); // inum
  i32 size = bfsGetSize(inum); // file size
  i32 cursor = bfsTell(fd); // cursor starting position
  i32 currFBN = cursor / BYTESPERBLOCK; //first block to be wrote to
  i32 lastFBN = (cursor + numb) / BYTESPERBLOCK; // last block to be wrote to
  if (cursor + numb > size) { // if last block is past EOF, extend file to last block
    bfsExtend(inum, lastFBN); 
    bfsSetSize(inum, cursor + numb);
  }

  i8 bioBuff[BYTESPERBLOCK];
  i32 currCursor = cursor; // where the cursor is currently in the current block
  while (currFBN <= lastFBN) { // while there is a file block to write to
    bfsRead(inum, currFBN, bioBuff); // read the block into bioBuff
    i32 start = currCursor - (currFBN * BYTESPERBLOCK); // start == current cursor position - starting point of current block
    i32 write = min(numb - (currCursor - cursor), ((currFBN + 1) * BYTESPERBLOCK) - currCursor); // write == min(remaining bytes to write, amount that can be wrote in block from start)
    memcpy(bioBuff + start, buf + (currCursor - cursor), write); // write into bioBuff[startBuff-endBuff] the next buf contents to be written
    bioWrite(bfsFbnToDbn(inum, currFBN), bioBuff); // set the current block to the newly written block
    currCursor += write; // move cursor by the amount of bytes written
    currFBN++; // move to next block
  }
  fsSeek(fd, numb, SEEK_CUR); // set cursor
  return 0;
}
