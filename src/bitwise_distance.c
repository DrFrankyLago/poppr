/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#
# This software was authored by Zhian N. Kamvar and Javier F. Tabima, graduate
# students at Oregon State University; and Dr. Nik Grünwald, an employee of
# USDA-ARS.
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for educational, research and non-profit purposes, without fee,
# and without a written agreement is hereby granted, provided that the statement
# above is incorporated into the material, giving appropriate attribution to the
# authors.
#
# Permission to incorporate this software into commercial products may be
# obtained by contacting USDA ARS and OREGON STATE UNIVERSITY Office for
# Commercialization and Corporate Development.
#
# The software program and documentation are supplied "as is", without any
# accompanying services from the USDA or the University. USDA ARS or the
# University do not warrant that the operation of the program will be
# uninterrupted or error-free. The end-user understands that the program was
# developed for research purposes and is advised not to rely exclusively on the
# program for any reason.
#
# IN NO EVENT SHALL USDA ARS OR OREGON STATE UNIVERSITY BE LIABLE TO ANY PARTY
# FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
# LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
# EVEN IF THE OREGON STATE UNIVERSITY HAS BEEN ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE. USDA ARS OR OREGON STATE UNIVERSITY SPECIFICALLY DISCLAIMS ANY
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE AND ANY STATUTORY
# WARRANTY OF NON-INFRINGEMENT. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
# BASIS, AND USDA ARS AND OREGON STATE UNIVERSITY HAVE NO OBLIGATIONS TO PROVIDE
# MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
#
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

#include <stdio.h>
#include <Rinternals.h>
#include <Rdefines.h>
#include <R.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <omp.h>

// TODO: Questions:
//          Where do we want the wrapper function? Distances.r? Something new?
//          Do we want to handle missing data in this function, or in the wrapper?
//          How do we want to handle the missing data? Always match? Never match? Population mean? 
//          Should it be in rdname genetic_distance, or something else?

// Assumptions:
//  All genotypes have the same number of SNPs available.
//  All SNPs are diploid.

// TODO: Informative header
// TODO: Clean up the comments and code to make it more readable
// TODO: Parallelize it if need be. It would be trivial to parallelize, but it's already pretty fast.
// TODO: Write an R wrapper function
// TODO: Write more R functions that make use of the data this spits out
// TODO: Handle missing data, either in this file or in the R wrapper.
// TODO: Make unit tests for the R function, including a test to make sure
//       x <- new("genlight", lapply(1:50, function(i) sample(c(0,1,2), 1e6, prob=c(.25, .5, .25), replace=TRUE)))
//       mean(bitwise.dist(x)) is roughly around .625
//       mean(bitwise.dist(x,diff=FALSE)) is roughly .375
//       mean(bitwise.dist(x,percent=FALSE)) is roughly 625000
//       and so on

struct zygosity
{
  char c1;  // A 32 bit fragment of one chromosome
  char c2;  // The corresponding fragment from the outer chromosome

  char cx;  // Heterozygous sites are indicated by 1's
  char ca;  // Homozygous dominant sites are indicated by 1's
  char cn;  // Homozygous recessive sites are indicated by 1's
};


void fill_zygosity(struct zygosity *ind);
char get_similarity_set(struct zygosity *ind1, struct zygosity *ind2);
int get_zeros(char sim_set);
int get_difference(struct zygosity *z1, struct zygosity *z2);

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Calculates the pairwise differences between samples in a genlight object. The
distances represent the number of sites between individuals which differ in 
zygosity.

Input: A genlight object containing samples of diploids.
Output: A distance matrix representing the number of differences in zygosity
        between each sample.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
SEXP bitwise_distance(SEXP genlight)
{
  SEXP R_out;
  SEXP R_gen_symbol;
  SEXP R_chr_symbol;  
  SEXP R_nap_symbol;
  SEXP R_gen;
  int num_gens;
  SEXP R_chr1_1;
  SEXP R_chr1_2;
  SEXP R_chr2_1;
  SEXP R_chr2_2;
  SEXP R_nap1;
  SEXP R_nap2;
  int next_missing_index_i;
  int next_missing_index_j;
  int next_missing_i;
  int next_missing_j;
  char mask;
  struct zygosity set_1;
  struct zygosity set_2;
  int i;
  int j;
  int k;

  int** distance_matrix;
  int cur_distance;
  char tmp_sim_set;

  
  PROTECT(R_gen_symbol = install("gen")); // Used for accessing the named elements of the genlight object
  PROTECT(R_chr_symbol = install("snp"));
  PROTECT(R_nap_symbol = install("NA.posi"));  

  // This will be a LIST of type LIST:RAW
  R_gen = getAttrib(genlight, R_gen_symbol);
  num_gens = XLENGTH(R_gen);

  PROTECT(R_out = allocVector(INTSXP, num_gens*num_gens));
  distance_matrix = R_Calloc(num_gens,int*);
  for(i = 0; i < num_gens; i++)
  {
    distance_matrix[i] = R_Calloc(num_gens,int);
  }

  next_missing_index_i = 0;
  next_missing_index_j = 0;
  next_missing_i = 0;
  next_missing_j = 0;
  mask = 0;
  tmp_sim_set = 0;

  // Loop through every genotype 
  for(i = 0; i < num_gens; i++)
  {
    R_chr1_1 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,i),R_chr_symbol),0); // Chromosome 1
    R_chr1_2 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,i),R_chr_symbol),1); // Chromosome 2
    R_nap1 = getAttrib(VECTOR_ELT(R_gen,i),R_nap_symbol); // Vector of the indices of missing values 
    // Loop through every other genotype
    for(j = 0; j < i; j++)
    {
      cur_distance = 0;
      // These will be arrays of type RAW
      R_chr2_1 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,j),R_chr_symbol),0); // Chromosome 1
      R_chr2_2 = VECTOR_ELT(getAttrib(VECTOR_ELT(R_gen,j),R_chr_symbol),1); // Chromosome 2
      R_nap2 = getAttrib(VECTOR_ELT(R_gen,j),R_nap_symbol); // Vector of the indices of missing values
      next_missing_index_j = 0; // Next set of chromosomes start back at index 0
      next_missing_j = (int)INTEGER(R_nap2)[next_missing_index_j] - 1; //Compensate for Rs 1 based indexing
      next_missing_index_i = 0;
      next_missing_i = (int)INTEGER(R_nap1)[next_missing_index_i] - 1;
      // Finally, loop through all the chunks of SNPs for these genotypes
      for(k = 0; k < XLENGTH(R_chr1_1); k++) 
      {
        set_1.c1 = (char)RAW(R_chr1_1)[k];
        set_1.c2 = (char)RAW(R_chr1_2)[k];
        fill_zygosity(&set_1);

        set_2.c1 = (char)RAW(R_chr2_1)[k];
        set_2.c2 = (char)RAW(R_chr2_2)[k];
        fill_zygosity(&set_2);

        tmp_sim_set = get_similarity_set(&set_1,&set_2);

        // Check for missing values and force them to match
        while(next_missing_index_i < XLENGTH(R_nap1) && next_missing_i < (k+1)*8 && next_missing_i >= (k*8) )
        {
          // Handle missing bit in both chromosomes and both samples with mask
          mask = 1 << (next_missing_i%8); // -1 to compensate for Rs 1 based indexing         
          tmp_sim_set |= mask; // Force the missing bit to match
          next_missing_index_i++; 
          next_missing_i = (int)INTEGER(R_nap1)[next_missing_index_i] - 1;
        }       
        // Repeat for j
        while(next_missing_index_j < XLENGTH(R_nap2) && next_missing_j < (k+1)*8 && next_missing_j >= (k*8))
        {
          // Handle missing bit in both chromosomes and both samples with mask
          mask = 1 << (next_missing_j%8);
          tmp_sim_set |= mask; // Force the missing bit to match
          next_missing_index_j++;
          next_missing_j = (int)INTEGER(R_nap2)[next_missing_index_j] - 1;
        }       

        // Add the distance from this word into the total between these two genotypes
        cur_distance += get_zeros(tmp_sim_set);
      }
      // Store the distance between these two genotypes in the distance matrix
      distance_matrix[i][j] = cur_distance;
      distance_matrix[j][i] = cur_distance;
    }
  } 

  // Fill the output matrix
  for(i = 0; i < num_gens; i++)
  {
    for(j = 0; j < num_gens; j++)
    {
      INTEGER(R_out)[i*num_gens+j] = distance_matrix[i][j];
    }
  }

  for(i = 0; i < num_gens; i++)
  {
    R_Free(distance_matrix[i]);
  }
  R_Free(distance_matrix);
  UNPROTECT(4); 
  return R_out;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Calculates the zygosity at each location of a given section. The zygosity struct
must have c1 and c2 filled before calling this function.

Input: A zygosity struct pointer with c1 and c2 filled for the given section.
Output: None. Fills the cx, ca, and cn values in the provided struct to indicate
        heterozygous, homozygous dominant, and homozygous recessive sites respectively,
        where 1's in each string represent the presence of that zygosity and 0's
        represent a different zygosity.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void fill_zygosity(struct zygosity *ind)
{
  ind->cx = ind->c1 ^ ind->c2;  // Bitwise Exclusive OR provides 1's only at heterozygous sites
  ind->ca = ind->c1 & ind->c2;  // Bitwise AND provides 1's only at homozygous dominant sites
  ind->cn = ~(ind->c1 | ind->c2); // Bitwise NOR provides 1's only at homozygous recessive sites
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Finds the locations at which two samples have differing zygosity.

Input: Two zygosity struct pointers with c1 and c2 filled, each representing the
       same section from two samples.
Output: A char representing a binary string of differences between the
        two samples in the given section. 0's represent a difference in zygosity
        at that location and 1's represent matching zygosity.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
char get_similarity_set(struct zygosity *ind1, struct zygosity *ind2)
{

  char s;   // The similarity values to be returned
  char sx;  // 1's wherever both are heterozygous
  char sa;  // 1's wherever both are homozygous dominant
  char sn;  // 1's wherever both are homozygous recessive

  sx = ind1->cx & ind2->cx;
  sa = ind1->ca & ind2->ca;
  sn = ind1->cn & ind2->cn;

  s = sx | sa | sn; // 1's wherever both individuals share the same zygosity  

  return s;
}


/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Calculates the number of zeros in a binary string. Used by get_difference to
find the number of differences between two samples in a given section.

Input: A char representing a binary string of differences between samples
       where 1's are matches and 0's are differences.
Output: The number of zeros in the argument value, representing the number of 
        differences between the two samples in the location used to generate
        the sim_set.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int get_zeros(char sim_set)
{
  int zeros = 0;
  int tmp_set = sim_set;
  // TODO: Standardize the number of bits used in everything to get rid of this magic number
  int digits = 8; //sizeof(char);
  int i;

  for(i = 0; i < digits; i++)
  {
    if(tmp_set%2 == 0)
    {
      zeros++;
    }
    tmp_set = tmp_set >> 1; // Drop the rightmost digit and shift the others over 1
  }

  return zeros;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Counts the number of differences between two partially filled zygosity structs.
c1 and c2 must be filled in both structs prior to calling this function.

Input: Two zygosity struct pointers representing the two sections to be compared.
       c1 and c2 must be filled in both structs.
Output: The number of locations in the given section that have differing zygosity
        between the two samples.
        cx, ca, and cn will be filled in both structs as a byproduct of this function.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int get_difference(struct zygosity *z1, struct zygosity *z2)
{
  int dif = 0;
  fill_zygosity(z1);
  fill_zygosity(z2);
  dif = get_zeros(get_similarity_set(z1,z2));

  return dif;
}


