//
// (C) 2021, E. Wes Bethel and Peter Ijeoma
// mpi_2dmesh.cpp

// usage:
//      mpi_2dmesh [args as follows]
// 
// command line arguments: 
//    -g 1|2|3  : domain decomp method, where 1=row-slab decomp, 2=column-slab decomp, 
//          3=tile decomp (OPTIONAL, default is -g 1, row-slab decomp)
//    -i filename : name of datafile containing raw unsigned bytes 
//          (OPTIONAL, default input filename in mpi_2dmesh.hpp)
//    -x XXX : specify the number of columns in the mesh, the width (REQUIRED)
//    -y YYY : specify the number of rows in the mesh, the height (REQUIRED)
//    -o filename : where output results will be written (OPTIONAL, default output
//          filename in mpi_2dmesh.hpp)
//    -a 1|2 : the action to perform: 1 means perform per-tile processing then write
//       output showing results, 2 means generate an output file with cells labeled as to
//       which rank they belong to (depends on -g 1|2|3 setting)
//       (OPTIONAL, default: -a 1, perform the actual processing)
//    -v : a flag that will trigger printing out the 2D vector array of Tile2D (debug)
//       (OPTIONAL, default value is no verbose debug output)
//
// Assumptions:
//
// Grid decompositions:
//       When creating tile-based decomps, will compute the number of tiles in each
//       dimension as sqrt(nranks); please use a number of ranks that is the square
//       of an integer, e.g., 4, 9, 16, 25, etc.

#include <iostream>
#include <vector>
#include <stdio.h>
#include <unistd.h>
#include <mpi.h>
#include <math.h>
#include <algorithm>
#include <omp.h>
#include <chrono>

#include "mpi_2dmesh.hpp"  // for AppState and Tile2D class

#define DEBUG_TRACE 0 

int parseArgs(int ac, char *av[], AppState *as)
{
   int rstat = 0;
   int c;

   while ( (c = getopt(ac, av, "va:g:x:y:i:")) != -1) {
      switch(c) {
         case 'a': {
                      int action = std::atoi(optarg == NULL ? "-1" : optarg);
                      if (action != MESH_PROCESSING && action != MESH_LABELING_ONLY)
                      {
                         printf("Error parsing command line: -a %s is an undefined action \n",optarg);
                         rstat = 1;
                      }
                      else
                         as->action=action;
                      break;
                   }

         case 'g': {
                      int decomp = std::atoi(optarg == NULL ? "-1" : optarg);
                      if (decomp != ROW_DECOMP && decomp != COLUMN_DECOMP && decomp != TILE_DECOMP)
                      {
                         printf("Error parsing command line: -g %s is an undefined decomposition\n",optarg);
                         rstat = 1;
                      }
                      else
                         as->decomp=decomp;
                      break;
                   }
         case 'x' : {
                       int xval = std::atoi(optarg == NULL ? "-1" : optarg);
                       if  (xval < 0 )
                       {
                          printf(" Error parsing command line: %d is not a valid mesh width \n",xval);
                          rstat = 1;
                       }
                       else
                          as->global_mesh_size[0] = xval;
                       break;
                    }
         case 'y' : {
                       int yval = std::atoi(optarg == NULL ? "-1" : optarg);
                       if  (yval < 0 )
                       {
                          printf(" Error parsing command line: %d is not a valid mesh height \n",yval);
                          rstat = 1;
                       }
                       else
                          as->global_mesh_size[1] = yval;
                       break;
                    }
         case 'i' : {
                       strcpy(as->input_filename, optarg);
                       break;
                    }
         case 'o' : {
                       strcpy(as->output_filename, optarg);
                       break;
                    }
         case 'v' : {
                        as->debug = 1;
                       break;
                    }
      } // end switch

   } // end while

   return rstat;
}

//
// computeMeshDecomposition:
// input: AppState *as - nranks, mesh size, etc.
// computes: tileArray, a 2D vector of Tile2D objects
//
// assumptions:
// - For tile decompositions, will use sqrt(nranks) tiles per axis
//
void computeMeshDecomposition(AppState *as, vector < vector < Tile2D > > *tileArray)
{
   int xtiles, ytiles;
   int ntiles;

   //printf(" in computeMeshDecomposition. \n");

   if (as->decomp == ROW_DECOMP) {

      //printf(" in computeMeshDecomposition. row decomposition\n");
      // in a row decomposition, each tile width is the same as the mesh width
      // the mesh is decomposed along height
      xtiles = 1;

      //  set up the y coords of the tile boundaries
      ytiles = as->nranks;
      int ylocs[ytiles+1];
      int ysize = as->global_mesh_size[1] / as->nranks; // size of each tile in y

      int yval = 0;
      for (int i = 0; i < ytiles; i++, yval += ysize) {
         ylocs[i] = yval;
      }
      ylocs[ytiles] = as->global_mesh_size[1];

      // then, create tiles along the y axis
      //printf(" in computeMeshDecomposition. row decomp - creating tiles\n");
      for (int i = 0; i < ytiles; i++)
      {
         vector < Tile2D > tiles;
         int width =  as->global_mesh_size[0];
         int height = ylocs[i+1]-ylocs[i];
         Tile2D t = Tile2D(0, ylocs[i], width, height, i);
         tiles.push_back(t);
         tileArray->push_back(tiles);
      }
   }
   else if (as->decomp == COLUMN_DECOMP) {
      // in a columne decomposition, each tile height is the same as the mesh height
      // the mesh is decomposed along width
      ytiles = 1;

      // set up the x coords of the tile boundaries
      xtiles = as->nranks; 
      int xlocs[xtiles+1];
      int xsize = as->global_mesh_size[0] / as->nranks; // size of each tile in x

      int xval=0;
      for (int i=0; i<xtiles; i++, xval+=xsize) {
         xlocs[i] = xval;
      }
      xlocs[xtiles] = as->global_mesh_size[0];

      // then, create tiles along the x axis
      vector < Tile2D > tile_row;
      for (int i=0; i<xtiles; i++)
      {
         int width =  xlocs[i+1]-xlocs[i];
         int height = as->global_mesh_size[1];
         Tile2D t = Tile2D(xlocs[i], 0, width, height, i);
         tile_row.push_back(t);
      }
      tileArray->push_back(tile_row);
   }
   else // assume as->decom == TILE_DECOMP
   {
      // to keep things simple, we  will assume sqrt(nranks) tiles in each of x and y axes.
      // if sqrt(nranks) is not an even integer, then this approach will result in some
      // ranks without tiles/work to do

      double root = sqrt(as->nranks);
      int nranks_per_axis = (int) root;

      xtiles = ytiles = nranks_per_axis;

      // set up x coords for tile boundaries
      int xlocs[xtiles+1];
      int xsize = as->global_mesh_size[0] / nranks_per_axis; // size of each tile in x

      int xval=0;
      for (int i=0; i<xtiles; i++, xval+=xsize) {
         xlocs[i] = xval;
      }
      xlocs[xtiles] = as->global_mesh_size[0];

      // set up y coords for tile boundaries
      int ylocs[ytiles+1];
      int ysize = as->global_mesh_size[1] / nranks_per_axis; // size of each tile in y

      int yval=0;
      for (int i=0; i<ytiles; i++, yval+=ysize) {
         ylocs[i] = yval;
      }
      ylocs[ytiles] = as->global_mesh_size[1];

      // now, build 2D array of tiles
      int rank=0;
      for (int j = 0; j < ytiles; j++) {  // fix me
         vector < Tile2D > tile_row;
         for (int i=0; i < xtiles; i++) {
            int width, height;
            width = xlocs[i+1]-xlocs[i];
            height = ylocs[j+1]-ylocs[j];
            Tile2D t = Tile2D(xlocs[i], ylocs[j], width, height, rank++);
            tile_row.push_back(t);
         }
         tileArray->push_back(tile_row);
      }
   }
}

void write_output_labels(AppState as, vector < vector < Tile2D > >tileArray)
{
   // create a buffer of ints, we will set buf[i,j] to be a value
   // in the range 0..nranks-1 to reflect which rank is owner of
   // a particular buf[i,j] location.
      
   size_t xsize = as.global_mesh_size[0];
   size_t ysize = as.global_mesh_size[1];

   printf("\n\nWriting out mesh labels to a file \n");

   int meshRankLabels[as.global_mesh_size[0]*as.global_mesh_size[1]];

   for (off_t i = 0; i < xsize*ysize; i++)
      meshRankLabels[i] = -1;

   for (int row = 0; row < tileArray.size(); row++)
   {
      printf(" Row %d of the tileArray is of length %d \n", row, tileArray[row].size());

      for (int col = 0; col < tileArray[row].size(); col++)
      {  
         Tile2D t = tileArray[row][col];

         t.print(row, col); // row, col

         // for each tile, paint the meshRankLabels array with the tile's rank
         // at the mesh locations owned by the tile

         // base offset into the output buffer for this tile
         size_t bufOffset = t.yloc * xsize + t.xloc;

         for (int jj = 0; jj < t.height; jj++, bufOffset += xsize)
         {
            for (int ii = 0; ii < t.width; ii++)
               meshRankLabels[bufOffset+ii] = t.tileRank;
         }

         xsize = as.global_mesh_size[0]; // breakpoint anchor
      } // end loop over tileArray columns
      ysize = as.global_mesh_size[1]; // breakpoint anchor
   } // end loop over tileArray rows

   FILE *f = fopen(as.output_filename, "w");
   if (f == NULL) {
      perror(" Error opening output file ");
   }
   fwrite((void *)meshRankLabels, sizeof(int), xsize*ysize, f);
   fclose(f);
} // end writing an output buffer

void printTileArray(vector < vector < Tile2D > > & tileArray)
{
   printf("---- Contents of the tileArray, which is of length %d \n", tileArray.size());

   for (int row = 0; row < tileArray.size(); row++)
   {
      printf(" Row %d of the tileArray is of length %d \n", row, tileArray[row].size());
      for (int col = 0; col < tileArray[row].size(); col++)
      {  
         Tile2D t = tileArray[row][col];
         t.print(row, col);
      }
   }
}
 
float byteNormalize(unsigned char i)
{
   #define ONE_OVER_255 0.003921568627451
   return ((float)i * ONE_OVER_255);
}

void loadInputFile(AppState *as)
{
   // open the input file
   FILE *f = fopen(as->input_filename, "r");
   if (f == NULL)
   {
      perror(" Problem opening input file ");
      return ;
   }

   // create the landing buffer for the input data
   size_t nitems = as->global_mesh_size[0] * as->global_mesh_size[1];
   vector<unsigned char> buf(nitems);

   // read the input into the buffer
   fread((void *)buf.data(), sizeof(unsigned char), nitems, f);
   fclose(f);

   // create space for the float-converted buffer
   as->input_data_floats.resize(nitems);

   // now convert from byte, in range 0..255, to float, in range 0..1
   transform(buf.begin(), buf.end(), as->input_data_floats.begin(), byteNormalize);
}

unsigned char floatNormalize(float t)
{
   // assume t in range 0..1
   return ((unsigned char)(t*255.0));
}

void writeOutputFile(AppState &as)
{
   // open the output file
   FILE *f = fopen(as.output_filename, "w");
   if (f == NULL)
   {
      perror(" Problem opening output file ");
      return ;
   }

#if 0
   // this code writes out the output as floats rather than converting to ubyte
   size_t nitems = as.global_mesh_size[0] * as.global_mesh_size[1];
   fwrite((const void *)as.output_data_floats.data(), sizeof(float), nitems, f);
#endif
 
#if 1
   // create the landing buffer for the input data
   size_t nitems = as.output_data_floats.size();

   vector<unsigned char> buf(nitems);

   // now convert from byte, in range 0..255, to float, in range 0..1
   transform(as.output_data_floats.begin(), as.output_data_floats.end(), buf.begin(), floatNormalize);

   // write out the byte buffer
   fwrite((const void *)buf.data(), sizeof(unsigned char), nitems, f);
#endif

   fclose(f);
}

int msg_count = 0;
void sendStridedBuffer(float *srcBuf, 
      int srcWidth, int srcHeight, 
      int srcOffsetColumn, int srcOffsetRow, 
      int sendWidth, int sendHeight, 
      int fromRank, int toRank ) 
{
   int msgTag = 0;

   //
   // ADD YOUR CODE HERE
   // That performs sending of  data using MPI_Send(), going "fromRank" and to "toRank". The
   // data to be sent is in srcBuf, which has width srcWidth, srcHeight.
   // Your code needs to send a subregion of srcBuf, where the subregion is of size
   // sendWidth by sendHeight values, and the subregion is offset from the origin of
   // srcBuf by the values specificed by srcOffsetColumn, srcOffsetRow.
   //

   int start_index = srcOffsetRow*srcWidth+srcOffsetColumn;
   int data_size = sendWidth*sendHeight;
   float tile_data[data_size];
   int k = 0;

   MPI_Datatype send_subarray;
   int tile_data_size[] = {data_size};
   int sub_array_size[] = {data_size/10};
   int subarray_start_ind[] = {1};
   int numb_dims = 1;

   MPI_Type_create_subarray(numb_dims, tile_data_size, sub_array_size, subarray_start_ind, MPI_ORDER_C, MPI_FLOAT, &send_subarray);
   MPI_Type_commit(&send_subarray);

   //printf(" in sendStridedBuffer. from rank %d to rank %d srcwidth %d srcheight %d rowoffset %d coloffset %d sendwidth %d send height %d \n", fromRank, toRank, srcWidth, srcHeight, srcOffsetRow, srcOffsetColumn, sendWidth, sendHeight);

   if ((srcWidth == sendWidth) && (srcHeight == sendHeight) &&
       (srcOffsetRow == 0) && (srcOffsetColumn == 0))  // send the full bufer
   {
      //printf(" Rank  %d in sendStridedBuffer. will send full srcbuf \n", fromRank);
      MPI_Send(srcBuf, 1, send_subarray, toRank, msgTag, MPI_COMM_WORLD);
      msg_count++;
      //MPI_Send(srcBuf, sendWidth*sendHeight, MPI_FLOAT, toRank, msgTag, MPI_COMM_WORLD);
   }
   else
   {      
      //printf(" Rank %d in sendStridedBuffer. accumulating tile data \n", fromRank);
      //accumulate the data before sending
      //k = 0;
      for (int i = 0; i < sendHeight; i++, start_index += srcWidth)
      {
         for (int j = 0; j < sendWidth; j++, k++)
         {
            tile_data[k] = srcBuf[start_index+j];
         }
      }

      //printf(" Rank %d in sendStridedBuffer. sending tile data \n", fromRank);
      // send the tile data
      MPI_Send(tile_data, 1, send_subarray, toRank, msgTag, MPI_COMM_WORLD);
      msg_count++;
      //MPI_Send(tile_data, sendWidth*sendHeight, MPI_FLOAT, toRank, msgTag, MPI_COMM_WORLD);
   }

   MPI_Type_free(&send_subarray);
}

int msg_data_total = 0;
void recvStridedBuffer(float *dstBuf, 
      int dstWidth, int dstHeight, 
      int dstOffsetColumn, int dstOffsetRow, 
      int expectedWidth, int expectedHeight, 
      int fromRank, int toRank )
{
   int msgTag = 0;
   int recvSize[2];
   MPI_Status stat;
   int rcount;

   int data_size = expectedWidth*expectedHeight;
   int start_index = dstOffsetRow*dstWidth+dstOffsetColumn;
   float dst_data[data_size];

   MPI_Datatype recv_subarray;
   int tile_data_size[] = {data_size};
   int sub_array_size[] = {data_size/10};
   int subarray_start_ind[] = {1};
   int numb_dims = 1;

   MPI_Type_create_subarray(numb_dims, tile_data_size, sub_array_size, subarray_start_ind, MPI_ORDER_C, MPI_FLOAT, &recv_subarray);
   MPI_Type_commit(&recv_subarray);

   //
   // ADD YOUR CODE HERE
   // That performs receiving of data using MPI_Recv(), coming "fromRank" and destined for
   // "toRank". The size of the data that arrives will be of size expectedWidth by expectedHeight 
   // values. This incoming data is to be placed into the subregion of dstBuf that has an origin
   // at dstOffsetColumn, dstOffsetRow, and that is expectedWidth, expectedHeight in size.
   //

   //printf(" in recvStridedBuffer. from rank %d to rank %d dstwidth %d dstheight %d rowoffset %d coloffset %d expectedwidth %d expectedheight %d \n", fromRank, toRank, dstWidth, dstHeight, dstOffsetRow, dstOffsetColumn, expectedWidth, expectedHeight);

   if ((dstWidth == expectedWidth) && (dstHeight == expectedHeight) &&
       (dstOffsetColumn == 0) && (dstOffsetRow == 0))  // receive the entire buffer
   {
      //printf(" Rank %d in recvStridedBuffer. receiving full buffer \n", toRank);
      //MPI_Recv(dstBuf, expectedHeight*expectedWidth, MPI_FLOAT, fromRank, msgTag, MPI_COMM_WORLD, &stat);
      //MPI_Recv(dstBuf, 1, recv_subarray, fromRank, msgTag, MPI_COMM_WORLD, &stat);
      MPI_Recv(dstBuf, data_size, MPI_FLOAT, fromRank, msgTag, MPI_COMM_WORLD, &stat);
      MPI_Get_count(&stat, MPI_FLOAT, &rcount);
      msg_data_total += rcount;
   }
   else
   {
      //printf(" Rank %d in recvStridedBuffer. receiving tile data \n", toRank);
      //MPI_Recv(dst_data, expectedHeight*expectedWidth, MPI_FLOAT, fromRank, msgTag, MPI_COMM_WORLD, &stat);
      //MPI_Recv(dst_data, 1, recv_subarray, fromRank, msgTag, MPI_COMM_WORLD, &stat);
      MPI_Recv(&dst_data[0], data_size, MPI_FLOAT, fromRank, msgTag, MPI_COMM_WORLD, &stat);
      MPI_Get_count(&stat, MPI_FLOAT, &rcount);
      msg_data_total += rcount;

      // put the data into the destination appropriately
      //printf(" Rank %d in recvStridedBuffer. copying to destination from tile data \n", toRank);
      for (int row = 0; row < expectedHeight; row++, start_index += dstWidth)
      {
         for (int j = 0, k = 0; j < expectedWidth; j++, k++)
         {
            dstBuf[start_index+j] = dst_data[k];
         }
      }
   }

   MPI_Type_free(&recv_subarray);
}


//
// ADD YOUR CODE HERE
// that performs sobel filtering
// suggest using your cpu code from HW5, no OpenMP parallelism 
//

// sobel_filtered_pixel(): perform the sobel filtering at a given i,j location
float sobel_filtered_pixel(float *s, int i, int j , int rows, int cols, float *gx, float *gy)
{
   float t = 0.0;

   int s_index = i * cols + j; // s index at [i,j] the center
   float Gx, Gy;

   Gx = gx[0]*s[s_index-cols-1] + gx[1]*s[s_index-cols] + gx[2]*s[s_index-(cols+1)] + gx[3]*s[s_index-1] + gx[4]*s[s_index] + gx[5]*s[s_index+1] + gx[6]*s[s_index+cols-1] + gx[7]*s[s_index+cols] + gx[8]*s[s_index+cols+1];
   Gy = gy[0]*s[s_index-cols-1] + gy[1]*s[s_index-cols] + gy[2]*s[s_index-(cols+1)] + gy[3]*s[s_index-1] + gy[4]*s[s_index] + gy[5]*s[s_index+1] + gy[6]*s[s_index+cols-1] + gy[7]*s[s_index+cols] + gy[8]*s[s_index+cols+1];
   
   t = sqrt(Gx*Gx + Gy*Gy);
   return t;
}

//  do_sobel_filtering() will iterate over all input image pixels and invoke the
//  sobel_filtered_pixel() function at each (i,j) location of input to compute the
//  sobel filtered output pixel at location (i,j) in output.
void do_sobel_filtering(float *in, float *out, int rows, int cols)
{
   float Gx[] = {1.0, 0.0, -1.0, 2.0, 0.0, -2.0, 1.0, 0.0, -1.0};
   float Gy[] = {1.0, 2.0, 1.0, 0.0, 0.0, 0.0, -1.0, -2.0, -1.0};

   // initialize the out array to 0.0 since we will skip the edges of the source image
   for (int i = 0; i < cols*rows; i++)
   {
      out[i] = 0.0;
   }

   for (int i = 1; i < rows-1; i++)     // skip the edges by starting at row index 1
   {
      for (int j = 1; j < cols-1; j++)  // skip the edges by starting at column index 1
      {
         out[i*cols+j] = sobel_filtered_pixel(in, i, j, rows, cols, Gx, Gy);
      }
   }
}

void sobelAllTiles(int myrank, vector < vector < Tile2D > > & tileArray) {
   for (int row = 0; row < tileArray.size(); row++)
   {
      for (int col = 0; col < tileArray[row].size(); col++)
      {  
         Tile2D *t = &(tileArray[row][col]);

         if (t->tileRank == myrank)
         {
#if 0
            // debug code
            // v1: fill the output buffer with the value of myrank
            //            printf(" sobelAllTiles(): filling the output buffer of size=%d with myrank=%d\n:", t->outputBuffer.size(), myrank);
            //std::fill(t->outputBuffer.begin(), t->outputBuffer.end(), myrank);

            // v2. copy the input to the output, umodified
            //   std::copy(t->inputBuffer.begin(), t->inputBuffer.end(), t->outputBuffer.begin());
#endif
            // ADD YOUR CODE HERE
            // to call your sobel filtering code on each tile
            //printf(" in sobelAllTiles. - calling do_sobel_filtering \n");
            do_sobel_filtering(t->inputBuffer.data(), t->outputBuffer.data(), t->height, t->width);
         }
      }
   }
}

void scatterAllTiles(int myrank, vector < vector < Tile2D > > & tileArray, float *s, int global_width, int global_height)
{
   //printf(" in scatterAllTiles.\n");

#if DEBUG_TRACE
   printf(" Rank %d is entering scatterAllTiles \n", myrank);
#endif
   for (int row = 0; row < tileArray.size(); row++)
   {
      for (int col = 0; col < tileArray[row].size(); col++)
      {   
         Tile2D *t = &(tileArray[row][col]);

         if (myrank != 0 && t->tileRank == myrank)
         {
            int fromRank = 0;

            // receive a tile's buffer 
            t->inputBuffer.resize(t->width*t->height);
            t->outputBuffer.resize(t->width*t->height);
#if DEBUG_TRACE
            printf("scatterAllTiles() receive side:: t->tileRank=%d, myrank=%d, t->inputBuffer->size()=%d, t->outputBuffersize()=%d \n", t->tileRank, myrank, t->inputBuffer.size(), t->outputBuffer.size());
#endif
            //printf(" Rank %d in scatterAllTiles. about to do receive tile data \n", t->tileRank);
            recvStridedBuffer(t->inputBuffer.data(), t->width, t->height,
                  0, 0,  // offset into the tile buffer: we want the whole thing
                  t->width, t->height, // how much data coming from this tile
                  fromRank, myrank); 
         }
         else if (myrank == 0)
         {
            if (t->tileRank != 0) {
#if DEBUG_TRACE
               printf("scatterAllTiles() send side: t->tileRank=%d, myrank=%d, t->inputBuffer->size()=%d \n", t->tileRank, myrank, t->inputBuffer.size());
#endif
               //printf(" Rank %d in scatterAllTiles. about to do send tile data to rank %d \n", myrank, t->tileRank);
               sendStridedBuffer(s, // ptr to the buffer to send
                     global_width, global_height,  // size of the src buffer
                     t->xloc, t->yloc, // offset into the send buffer
                     t->width, t->height,  // size of the buffer to send,
                     myrank, t->tileRank);
            }
            else // rather then have rank 0 send to rank 0, just do a strided copy into a tile's input buffer
            {
               t->inputBuffer.resize(t->width*t->height);
               t->outputBuffer.resize(t->width*t->height);

               off_t s_offset=0, d_offset=0;
               float *d = t->inputBuffer.data();
               //printf(" Rank %d in scatterAllTiles. about to do memcpy \n", t->tileRank);
               //s_offset = t->yloc*global_width+t->xloc;  // Peter ijeoma - starting index

               for (int j = 0; j < t->height; j++, s_offset+=global_width, d_offset+=t->width)
               {
                  memcpy((void *)(d+d_offset), (void *)(s+s_offset), sizeof(float)*t->width);
               }
            }
         }
      }
   } // loop over 2D array of tiles

#if DEBUG_TRACE
   MPI_Barrier(MPI_COMM_WORLD);
   if (myrank == 1){
      printf("\n\n ----- rank=%d, inside scatterAllTiles debug printing of the tile array \n", myrank);
      printTileArray(tileArray);
   }
   MPI_Barrier(MPI_COMM_WORLD);
#endif
      
}

void gatherAllTiles(int myrank, vector < vector < Tile2D > > & tileArray, float *d, int global_width, int global_height)
{

   for (int row = 0; row < tileArray.size(); row++)
   {
      for (int col = 0; col < tileArray[row].size(); col++)
      {  
         Tile2D *t = &(tileArray[row][col]);

#if DEBUG_TRACE
         printf("gatherAllTiles(): t->tileRank=%d, myrank=%d, t->outputBuffer->size()=%d \n", t->tileRank, myrank, t->outputBuffer.size());
#endif

         if (myrank != 0 && t->tileRank == myrank)
         {
            // send the tile's output buffer to rank 0
            sendStridedBuffer(t->outputBuffer.data(), // ptr to the buffer to send
               t->width, t->height,  // size of the src buffer
               0, 0, // offset into the send buffer
               t->width, t->height,  // size of the buffer to send,
               t->tileRank, 0);   // from rank, to rank
         }
         else if (myrank == 0)
         {
            if (t->tileRank != 0) {
               // receive a tile's buffer and copy back into the output buffer d
               recvStridedBuffer(d, global_width, global_height,
                     t->xloc, t->yloc,  // offset of this tile
                     t->width, t->height, // how much data coming from this tile
                     t->tileRank, myrank); 
            }
            else // copy from a tile owned by rank 0 back into the main buffer
            {
               float *s = t->outputBuffer.data();
               off_t s_offset = 0, d_offset = 0;
               d_offset = t->yloc * global_width + t->xloc;

               for (int j = 0; j < t->height; j++, s_offset += t->width, d_offset += global_width)
               {
                  memcpy((void *)(d+d_offset), (void *)(s+s_offset), sizeof(float)*t->width);
               }
            }
         }
      }
   } // loop over 2D array of tiles
}

int main(int ac, char *av[]) {

   AppState as;
   vector < vector < Tile2D > > tileArray;

   std::chrono::time_point<std::chrono::high_resolution_clock> start_time, end_time;
   std::chrono::duration<double> elapsed_scatter_time, elapsed_sobel_time, elapsed_gather_time;

   MPI_Init(&ac, &av);
  
   int myrank, nranks; 
   MPI_Comm_rank( MPI_COMM_WORLD, &myrank);
   MPI_Comm_size( MPI_COMM_WORLD, &nranks);

   as.myrank = myrank;
   as.nranks = nranks;

   //printf(" in main. about to parse args \n");

   if (parseArgs(ac, av, &as) != 0)
   {
      MPI_Finalize();
      return 1;
   }

   char hostname[256];
   gethostname(hostname, sizeof(hostname));

   printf("Hello world, I'm rank %d of %d total ranks running on <%s>\n", as.myrank, as.nranks, hostname);
   MPI_Barrier(MPI_COMM_WORLD);

#if DEBUG_TRACE
   if (as.myrank == 0)
      printf("\n\n ----- All ranks will computeMeshDecomposition \n");
#endif

   //printf(" in main. about to call computeMeshDecomposition \n");
   computeMeshDecomposition(&as, &tileArray);
   
   if (as.myrank == 0 && as.debug==1) // print out the AppState and tileArray
   {
      as.print();
      printTileArray(tileArray);
   }

   MPI_Barrier(MPI_COMM_WORLD);

   if (as.action == MESH_LABELING_ONLY) {
      if (as.myrank==0) {
         printf("\n\n Rank 0 is writing out mesh labels to a file \n");
         write_output_labels(as, tileArray);
      }
   }
   else 
   {
      // === rank 0 loads the input file 
      if (as.myrank == 0)
      {
#if DEBUG_TRACE
         printf("\n\n Rank 0 is loading input \n");
#endif
         loadInputFile(&as);
      }
      MPI_Barrier(MPI_COMM_WORLD);

      // ----------- scatter phase of processing

      // start the timer
      start_time = std::chrono::high_resolution_clock::now();

      //printf(" in main. about to scatterAllTiles \n");

      scatterAllTiles(as.myrank, tileArray, as.input_data_floats.data(), as.global_mesh_size[0], as.global_mesh_size[1]);

      // end the timer
      MPI_Barrier(MPI_COMM_WORLD);
      end_time = std::chrono::high_resolution_clock::now();
      elapsed_scatter_time = end_time - start_time;

      // ----------- the actual processing
      MPI_Barrier(MPI_COMM_WORLD);

      // start the timer
      start_time = std::chrono::high_resolution_clock::now();

      //printf(" in main. about to sobelAllTiles \n");

      sobelAllTiles(as.myrank, tileArray);

      // end the timer
      MPI_Barrier(MPI_COMM_WORLD);
      end_time = std::chrono::high_resolution_clock::now();
      elapsed_sobel_time = end_time - start_time;


      // ----------- gather processing

      // create output buffer space on rank 0
      if (as.myrank == 0) {
         as.output_data_floats.resize(as.global_mesh_size[0]*as.global_mesh_size[1]);
         // initialize to a known value outside the range of expected values 
         std::fill(as.output_data_floats.begin(), as.output_data_floats.end(), -1.0);
      }

      MPI_Barrier(MPI_COMM_WORLD);

      // start the timer
      start_time = std::chrono::high_resolution_clock::now();

      //printf(" in main. about to gatherAllTiles \n");

      gatherAllTiles(as.myrank, tileArray, as.output_data_floats.data(), as.global_mesh_size[0], as.global_mesh_size[1]);

      // end the timer
      MPI_Barrier(MPI_COMM_WORLD);
      end_time = std::chrono::high_resolution_clock::now();
      elapsed_gather_time = end_time - start_time;

      // === write output

      if (as.myrank == 0) // only rank 0 writes data to disk
      {
         writeOutputFile(as);
      }
   }

   MPI_Barrier(MPI_COMM_WORLD);

   //printf(" in main. will print runtimes - done \n");

   if (as.myrank == 0) {
      printf("\n\nTiming results from rank 0: \n");
      printf("\tScatter time:\t%6.4f (ms) \n", elapsed_scatter_time*1000.0);
      printf("\tSobel time:\t%6.4f (ms) \n", elapsed_sobel_time*1000.0);
      printf("\tGather time:\t%6.4f (ms) \n", elapsed_gather_time*1000.0);
      printf("\n\nNumber of messages and total data moved from rank 0: \n");
      printf("\tNumber of messages:\t%d \n", msg_count);
      printf("\tTotal data moved:\t%d \n", msg_data_total);
   }

   MPI_Finalize();
   return 0;
}
// EOF
