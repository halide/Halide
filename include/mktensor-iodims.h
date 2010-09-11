/*
 * Copyright (c) 2003, 2007-8 Matteo Frigo
 * Copyright (c) 2003, 2007-8 Massachusetts Institute of Technology
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "api.h"

tensor *MKTENSOR_IODIMS(int rank, const IODIM *dims, int is, int os)
{
     int i;
     tensor *x = X(mktensor)(rank);

     if (FINITE_RNK(rank)) {
          for (i = 0; i < rank; ++i) {
               x->dims[i].n = dims[i].n;
               x->dims[i].is = dims[i].is * is;
               x->dims[i].os = dims[i].os * os;
          }
     }
     return x;
}

static int iodims_kosherp(int rank, const IODIM *dims, int allow_minfty)
{
     int i;

     if (rank < 0) return 0;

     if (allow_minfty) {
	  if (!FINITE_RNK(rank)) return 1;
	  for (i = 0; i < rank; ++i)
	       if (dims[i].n < 0) return 0;
     } else {
	  if (!FINITE_RNK(rank)) return 0;
	  for (i = 0; i < rank; ++i)
	       if (dims[i].n <= 0) return 0;
     }

     return 1;
}

int GURU_KOSHERP(int rank, const IODIM *dims,
		 int howmany_rank, const IODIM *howmany_dims)
{
     return (iodims_kosherp(rank, dims, 0) &&
	     iodims_kosherp(howmany_rank, howmany_dims, 1));
}
