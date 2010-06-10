/**
 * \file q-ranker.cpp
 * AUTHOR: Barbara Frewen and Marina Spivak
 * CREATE DATE: November 25, 2008
 * \brief Given as input a directory containing binary psm files and
 *        a protein database, run q-ranker and return a text file
 *        with results.
 *
 * Handles at most 4 files (target and decoy).  Looks for .csm files
 * in the input directory and for corresponding -decoy[123].csm files.
 * Multiple target files in the given directory are concatinated
 * together and presumed to be non-overlaping parts of the same ms2
 * file.
 *
 * Copied from match_analysis.c with only the percolator functionality
 * kept.
 ****************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "carp.h"
#include "crux-utils.h"
#include "objects.h"
#include "parameter.h"
#include "q-ranker.h"
#include "protein.h"
#include "peptide.h"
#include "spectrum.h"
#include "parse_arguments.h" 
#include "spectrum_collection.h"
#include "generate_peptides_iterator.h"
#include "scorer.h"
#include "match.h"
#include "match_collection.h"
#include "QRankerCInterface.h"
#include "output-files.h"

/**
 * \brief Analyze matches using the q-ranker algorithm
 * 
 * Runs Marina Spivak's q-ranker algorithm on the PSMs in the psm_result_folder
 * for a search against the sequence database fasta_file. Optionally 
 * puts the q-ranker PSM feature vectors into feature_file, if it is 
 * not NULL.
 * \returns a pointer to a MATCH_COLLECTION_T object
 * \callgraph
 */
MATCH_COLLECTION_T* run_qranker(
  char* psm_result_folder, 
  char* fasta_file, 
  OutputFiles& output){ 

  unsigned int number_features = 20;
  double* features = NULL;    
  double* results_q = NULL;
  double* results_score = NULL;
  double pi0 = get_double_parameter("pi0");
  char** feature_names = generate_feature_name_array();
  MATCH_ITERATOR_T* match_iterator = NULL;
  MATCH_COLLECTION_T* match_collection = NULL;
  MATCH_COLLECTION_T* target_match_collection = NULL;
  MATCH_T* match = NULL;
  int set_idx = 0;
  
  output.writeFeatureHeader(feature_names, number_features);

    // create MATCH_COLLECTION_ITERATOR_T object
  // which will read in the serialized output PSM results and return
  // first the match_collection of TARGET followed by 
  // the DECOY* match_collections.
  int num_decoys = 0;
  
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator1 =
    new_match_collection_iterator(psm_result_folder, fasta_file, &num_decoys);

  if( match_collection_iterator1 == NULL ){
    carp(CARP_FATAL, "Failed to create a match collection iterator");
    exit(1);
  }
  int num_sets = get_match_collection_iterator_number_collections(match_collection_iterator1);
  int* num_spectra = (int*)mycalloc(num_sets, sizeof(int));
  // iterate over each, TARGET, DECOY 1..3 match_collection sets
  int iterations = 0;
  while(match_collection_iterator_has_next(match_collection_iterator1)){
    
    
    
    // get the next match_collection
    match_collection = 
      match_collection_iterator_next(match_collection_iterator1);
    

    num_spectra[iterations] = get_match_collection_match_total(match_collection);
    iterations++;
    carp(CARP_DEBUG, "Match collection iteration: %i" , iterations);    
    }

  // Call that initiates q-ranker
  
  // create MATCH_COLLECTION_ITERATOR_T object
  // which will read in the serialized output PSM results and return
  // first the match_collection of TARGET followed by 
  // the DECOY* match_collections.
  num_decoys = 0;
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator =
    new_match_collection_iterator(psm_result_folder, fasta_file, &num_decoys);

  if( match_collection_iterator == NULL ){
    carp(CARP_FATAL, "Failed to create a match collection iterator");
    exit(1);
  }
  carp(CARP_DETAILED_DEBUG, "Created the match collection iterator");

  
  // iterate over each, TARGET, DECOY 1..3 match_collection sets
  iterations = 0;
  while(match_collection_iterator_has_next(match_collection_iterator)){
    
    carp(CARP_DEBUG, "Match collection iteration: %i" , iterations++);

    // get the next match_collection
    match_collection = 
      match_collection_iterator_next(match_collection_iterator);
    
    // intialize q-ranker, using information from first match_collection
    if(set_idx == 0){
      // the first match_collection is the target_match_collection
      target_match_collection = match_collection;
      
      // result array that stores the algorithm scores
      results_q = (double*)mycalloc(
          get_match_collection_match_total(match_collection), sizeof(double));
      results_score = (double*)mycalloc(
          get_match_collection_match_total(match_collection), sizeof(double));
          
      // Call that initiates q-ranker
      qcInitiate(
          (NSet)get_match_collection_iterator_number_collections(
                   match_collection_iterator), 
          number_features, 
          num_spectra, 
          feature_names, 
          pi0);
      
      free(num_spectra);
      
      // Call that sets verbosity level
      // 0 is quiet, 2 is default, 5 is more than you want
      if(verbosity < CARP_ERROR){
	qcSetVerbosity(0);
      }    
      else if(verbosity < CARP_INFO){
	qcSetVerbosity(1);
      }
      else{
	qcSetVerbosity(5);
      }
    }

    // create iterator, to register each PSM feature to q-ranker
    match_iterator = new_match_iterator(match_collection, XCORR, FALSE);
    
    while(match_iterator_has_next(match_iterator)){
      match = match_iterator_next(match_iterator);
      // Register PSM with features to q-ranker    
      features = get_match_percolator_features(match, match_collection);

      output.writeMatchFeatures(match, features, number_features);
      qcRegisterPSM((SetType)set_idx, get_match_sequence_sqt(match), features);
      
      free(features);
    }

    

    // ok free & update for next set
    // MEMLEAK 
    free_match_iterator(match_iterator);

    // don't free the target_match_collection
    if(set_idx != 0){
      free_match_collection(match_collection);
    }

    ++set_idx;
  } // end iteratation over each, TARGET, DECOY 1..3 match_collection sets

  /***** Q-RANKER run *********/

  // Start processing
  qcExecute(); 
  
  /* Retrieving target scores and qvalues after 
   * processing, the array should be numSpectra long and will be filled in 
   * the same order as the features were inserted */
  qcGetScores(results_score, results_q); 
       
  // fill results for QRANKER_Q_VALUE
  fill_result_to_match_collection(
      target_match_collection, results_q, QRANKER_Q_VALUE, TRUE);
  
  // fill results for QRANKER_SCORE
  fill_result_to_match_collection(
      target_match_collection, results_score, QRANKER_SCORE, FALSE);
   
  // Function that should be called after processing finished
  qcCleanUp();
  
  // free names
  unsigned int name_idx;
  for(name_idx=0; name_idx < number_features; ++name_idx){
    free(feature_names[name_idx]);
  }
  free(feature_names);
  free(results_q);
  free(results_score);
  free_match_collection_iterator(match_collection_iterator);

  // TODO put free back in. took out because glibc claimed it was corrupted
  // double linked list
  // free_parameters();
  return target_match_collection;
}



/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */
