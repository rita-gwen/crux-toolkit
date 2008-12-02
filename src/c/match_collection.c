/*********************************************************************//**
 * \file match_collection.c
 * \brief A set of peptide spectrum matches for one spectrum.
 *
 * Methods for creating and manipulating match_collections.   
 * Creating a match collection generates all matches (searches a
 * spectrum against a database.
 *
 * AUTHOR: Chris Park
 * CREATE DATE: 11/27 2006
 * $Revision: 1.85 $
 ****************************************************************************/
#include "match_collection.h"

//static BOOLEAN_T is_first_spectrum = TRUE;

/* Private data types (structs) */

/**
 * \struct match_collection
 * \brief An object that contains a set of match objects.
 *
 * May contain matches for one spectrum or many spectra. 
 * 
 * 
 */
struct match_collection{
  MATCH_T* match[_MAX_NUMBER_PEPTIDES]; ///< array of match object
  BOOLEAN_T scored_type[_SCORE_TYPE_NUM]; 
    ///< has the score type been computed in each match
  int experiment_size; 
    ///< total peptide count from the database before any truncation
  int match_total; ///< total_match_count
  SCORER_TYPE_T last_sorted; 
    ///< the last type by which it's been sorted ( -1 if unsorted)
  BOOLEAN_T iterator_lock; 
    ///< has an itterator been created? if TRUE can't manipulate matches
  int charge; ///< charge of the associated spectrum
  BOOLEAN_T null_peptide_collection; ///< are the searched peptides null

  // values used for various scoring functions.
  float delta_cn; ///< the difference in top and second Xcorr scores
  float sp_scores_mean;  ///< the mean value of the scored peptides sp score
  float mu; 
  ///< EVD parameter Xcorr(characteristic value of extreme value distribution)
  float l_value; 
  ///< EVD parameter Xcorr(decay constant of extreme value distribution)
  int top_fit_sp; 
  ///< The top ranked sp scored peptides to use as EXP_SP parameter estimation
  float base_score_sp; 
 ///< The lowest sp score within top_fit_sp, used as the base to rescale sp
  float eta;  ///< The eta parameter for the Weibull distribution.
  float beta; ///< The beta parameter for the Weibull distribution.
  float shift; ///< The location parameter for the Weibull distribution.

  // The following features (post_*) are only valid when
  // post_process_collection boolean is TRUE 
  BOOLEAN_T post_process_collection; 
  ///< Is this a post process match_collection?
  int post_protein_counter_size; 
  ///< the size of the protein counter array, usually the number of proteins in database
  int* post_protein_counter; 
  ///< the counter for how many each protein has matches other PSMs
  int* post_protein_peptide_counter; 
  ///< the counter for how many each unique peptides each protein has matches other PSMs
  HASH_T* post_hash; ///< hash table that keeps tracks of the peptides
  BOOLEAN_T post_scored_type_set; 
  ///< has the scored type been confirmed for the match collection,
  // set after the first match collection is extended
};

/**
 *\struct match_iterator
 *\brief An object that iterates over the match objects in the
 * specified match_collection for the specified score type (SP, XCORR)
 */
struct match_iterator{
  MATCH_COLLECTION_T* match_collection; 
                            ///< the match collection to iterate -out
  SCORER_TYPE_T match_mode; ///< the current working score (SP, XCORR)
  int match_idx;            ///< current match to return
  int match_total;          ///< total_match_count
};

/**
 * \struct match_collection_iterator
 * \brief An object that iterates over the match_collection objects in
 * the specified directory of serialized match_collections 
 */
struct match_collection_iterator{
  DIR* working_directory; 
  ///< the working directory for the iterator to find match_collections
  char* directory_name; ///< the directory name in char
  DATABASE_T* database; ///< the database for which the match_collection
  int number_collections; 
  ///< the total number of match_collections in the directory (target+decoy)
  int collection_idx;  ///< the index of the current collection to return
  MATCH_COLLECTION_T* match_collection; ///< the match collection to return
  BOOLEAN_T is_another_collection; 
  ///< is there another match_collection to return?
};

/******* Private function declarations, described in definintions below ***/

BOOLEAN_T score_match_collection_sp(
  MATCH_COLLECTION_T* match_collection, 
  SPECTRUM_T* spectrum, 
  int charge,
  GENERATE_PEPTIDES_ITERATOR_T* peptide_iterator
  );

BOOLEAN_T score_match_collection_xcorr(
  MATCH_COLLECTION_T* match_collection,
  SPECTRUM_T* spectrum,
  int charge
  );

BOOLEAN_T score_match_collection_logp_exp_sp(
  MATCH_COLLECTION_T* match_collection, 
  int peptide_to_score 
  );

BOOLEAN_T score_match_collection_logp_bonf_exp_sp(
  MATCH_COLLECTION_T* match_collection, 
  int peptide_to_score 
  );

BOOLEAN_T score_match_collection_logp_weibull_sp(
  MATCH_COLLECTION_T* match_collection, 
  int peptide_to_score 
  );

BOOLEAN_T score_match_collection_logp_bonf_weibull_sp(
  MATCH_COLLECTION_T* match_collection, 
  int peptide_to_score 
  );

BOOLEAN_T score_match_collection_logp_weibull_xcorr(
  MATCH_COLLECTION_T* match_collection, 
  int peptide_to_score 
  );

BOOLEAN_T score_match_collection_logp_bonf_weibull_xcorr(
  MATCH_COLLECTION_T* match_collection, 
  int peptide_to_score 
  );

BOOLEAN_T score_match_collection_logp_evd_xcorr(
  MATCH_COLLECTION_T* match_collection, 
  int peptide_to_score 
  );

BOOLEAN_T score_match_collection_logp_bonf_evd_xcorr(
  MATCH_COLLECTION_T* match_collection, 
  int peptide_to_score 
  );

BOOLEAN_T estimate_evd_parameters(
  MATCH_COLLECTION_T* match_collection, 
  int sample_count, 
  SCORER_TYPE_T score_type, 
  SPECTRUM_T* spectrum,    
  int charge       
  );

BOOLEAN_T estimate_exp_sp_parameters(
  MATCH_COLLECTION_T* match_collection, 
  int top_count 
  );

BOOLEAN_T estimate_weibull_parameters(
  MATCH_COLLECTION_T* match_collection, 
  SCORER_TYPE_T score_type,
  int sample_count, 
  SPECTRUM_T* spectrum,
  int charge
  );

void truncate_match_collection(
  MATCH_COLLECTION_T* match_collection, 
  int max_rank,     
  SCORER_TYPE_T score_type 
  );

BOOLEAN_T extend_match_collection(
  MATCH_COLLECTION_T* match_collection, 
  DATABASE_T* database, 
  FILE* result_file   
  );

BOOLEAN_T add_match_to_match_collection(
  MATCH_COLLECTION_T* match_collection, 
  MATCH_T* match 
  );

void update_protein_counters(
  MATCH_COLLECTION_T* match_collection, 
  PEPTIDE_T* peptide  
  );

/********* end of function declarations *******************/


/**
 * \returns An (empty) match_collection object.
 */
MATCH_COLLECTION_T* allocate_match_collection()
{
  MATCH_COLLECTION_T* match_collection =
    (MATCH_COLLECTION_T*)mycalloc(1, sizeof(MATCH_COLLECTION_T));
    
  // loop over to set all score type to FALSE
  int score_type_idx = 0;
  for(; score_type_idx < _SCORE_TYPE_NUM; ++score_type_idx){
    match_collection->scored_type[score_type_idx] = FALSE;
  }
  
  // set last score to -1, thus nothing has been done yet
  match_collection->last_sorted = -1;
  match_collection->iterator_lock = FALSE;
  match_collection->post_process_collection = FALSE;
  match_collection->null_peptide_collection = FALSE;
  
  return match_collection;
}

/**
 * /brief Free the memory allocated for a match collection
 */
void free_match_collection(
  MATCH_COLLECTION_T* match_collection ///< the match collection to free -out
  )
{
  // delete all matches (decrement the pointer count to each match object)
  while(match_collection->match_total > 0){
    --match_collection->match_total;
    free_match(match_collection->match[match_collection->match_total]);
    match_collection->match[match_collection->match_total] = NULL;
  }
  
  // free post_process_collection specific memory
  if(match_collection->post_process_collection){
    // free protein counter
    free(match_collection->post_protein_counter);
    
    // free protein peptide counter
    free(match_collection->post_protein_peptide_counter);
  
    // free hash table
    free_hash(match_collection->post_hash);
  }

  free(match_collection);
}

// TODO (BF 1-28-08): max_rank, scores, offset can be taken from parameter.c

/**
 * \brief Creates a new match collection by searching a database
 * for matches to a spectrum. in .c
 *
 * \details This is the main spectrum searching routine.  Allocates memory for
 * the match collection. Creates a peptide iterator for given mass
 * window. Performs preliminary scoring on all candidate
 * peptides. Performs primary scoring on the <max_rank> best-scoring
 * peptides. Estimates EVD parameters. in .c
 *
 * \returns A new match_collection object that is scored by score_type
 * and contains the top max_rank matches in .c
 * \callgraph
 */
MATCH_COLLECTION_T* new_match_collection_from_spectrum(
 SPECTRUM_T* spectrum, 
    ///< the spectrum to match peptides in -in
 int charge,       
   ///< the charge of the spectrum -in
 int max_rank,     
   ///< max number of top rank matches to keep from SP -in
 SCORER_TYPE_T prelim_score, 
   ///< the preliminary score type (SP) -in
 SCORER_TYPE_T score_type, 
   ///< the score type (XCORR, LOGP_EXP_SP, LOGP_BONF_EXP_SP) -in
 float mass_offset,  
   ///< the mass offset from neutral_mass to search for candidate peptides -in
 BOOLEAN_T null_peptide_collection,
   ///< is this match_collection a null peptide collection? -in
 INDEX_T* index,      ///< the index source of peptides
 DATABASE_T* database ///< the database (fasta) soruce of peptides
 )
{
  MATCH_COLLECTION_T* match_collection = allocate_match_collection();
  
  // set charge of match_collection creation
  match_collection->charge = charge;
  match_collection->null_peptide_collection = null_peptide_collection;

  //int top_rank_for_p_value = get_int_parameter("top-rank-p-value");
  int top_rank_for_p_value = get_int_parameter("top-match");
  if( get_int_parameter("max-sqt-result") > top_rank_for_p_value ){
    top_rank_for_p_value = get_int_parameter("max-sqt-result");
  }
  int sample_count = get_int_parameter("sample-count");
  int top_fit_sp = get_int_parameter("top-fit-sp");
  
  // move out of crux index dir
  /*  if(!is_first_spectrum){
    chdir("..");
  }else{
    is_first_spectrum = FALSE;
    }*/
  
  // create a generate peptide iterator
  // FIXME use neutral_mass for now, but should allow option to change
  GENERATE_PEPTIDES_ITERATOR_T* peptide_iterator =  
    new_generate_peptides_iterator_from_mass(
        get_spectrum_neutral_mass(spectrum, charge) + mass_offset,
        index, database
        );
  
  /***************Preliminary scoring**************************/
  // When creating match objects for first time, must set the
  // null peptide boolean parameter
  
  // score SP match_collection
  if(prelim_score == SP){
    if(!score_match_collection_sp(
          match_collection, 
          spectrum, 
          charge, 
          peptide_iterator)){
      carp(CARP_ERROR, "Failed to score match collection for SP");
      free_match_collection(match_collection);
      return NULL;
    }
    if (match_collection->match_total == 0){
      carp(CARP_WARNING, "No matches found for spectrum %i charge %i",
          get_spectrum_first_scan(spectrum), charge);
      free_generate_peptides_iterator(peptide_iterator);
      free_match_collection(match_collection);
      return NULL;
    }
  }// else no other prelim scores considered! No spec searched!


  /******* Scoring and estimating score distribution parameters ***/
  // The only supported distribution is the weibull with bonf correction 

  carp(CARP_DETAILED_INFO,"Number matches after preliminary scoring = %i",match_collection->match_total);

  BOOLEAN_T success = TRUE;
  if(score_type == LOGP_WEIBULL_XCORR || 
     score_type == LOGP_BONF_WEIBULL_XCORR){
    success = estimate_weibull_parameters(match_collection, XCORR, 
                                          sample_count, spectrum, charge);
  } else if(score_type == LOGP_WEIBULL_SP || 
            score_type == LOGP_BONF_WEIBULL_SP){
    success = estimate_weibull_parameters(match_collection, SP, 
                                          sample_count, spectrum, charge);
  }
  // Remaining are legacy scoring functions
  
  // For evd parameter estimation, sample before truncating match
  // collection, i.e. from the entire peptide distribution
  else if(score_type == LOGP_EVD_XCORR || score_type == LOGP_BONF_EVD_XCORR){
    estimate_evd_parameters(
        match_collection, 
        sample_count, 
        XCORR, 
        spectrum, 
        charge);
  }
  // if scoring for LOGP_EXP_SP, LOGP_BONF_EXP_SP estimate parameters
  else if(score_type == LOGP_EXP_SP || score_type == LOGP_BONF_EXP_SP){
    estimate_exp_sp_parameters(match_collection, top_fit_sp);
  }

  // estimating parameters function will return false if too few matches
  // spectrum is not scored, return as such 
  if( success == FALSE ){
    free_match_collection(match_collection);
    return NULL;
  }

  carp(CARP_DETAILED_INFO,"Number matches after parameter estimation = %i",match_collection->match_total);

  // save only the top max_rank matches from prelim_scoring
  truncate_match_collection(match_collection, max_rank, prelim_score);
  
  carp(CARP_DETAILED_INFO,"Number matches after truncation = %i",match_collection->match_total);
  
  /***************Main scoring*******************************/
  // The only supported types of primary score are xcorr,
  // pval of sp (sp-logp), pval of xcorr (xcorr-logp)

  if( score_type == XCORR ){
    if(!score_match_collection_xcorr(match_collection, spectrum, charge)){
      carp(CARP_ERROR, 
           "Failed to score match collection for XCORR, spectrum %d charge %d",
           get_spectrum_first_scan(spectrum), charge);
    }
  }else if(score_type == LOGP_BONF_WEIBULL_XCORR){
    // we have to score for xcorr b/c in estimating params, we only scored
    // a subset of matches
    score_match_collection_xcorr(match_collection, spectrum, charge);
    if(!score_match_collection_logp_bonf_weibull_xcorr(match_collection, 
                                                       top_rank_for_p_value)){
      carp(CARP_ERROR, 
           "Failed to score match collection for LOGP_BONF_WEIBULL_XCORR");
    }
  }else if(score_type == LOGP_WEIBULL_SP){
    carp(CARP_DEBUG, "Scoring match collection for LOGP_WEIBULL_SP");
    if(!score_match_collection_logp_weibull_sp( match_collection, 
                                                top_rank_for_p_value)){ 
      carp(CARP_ERROR, "Failed to score match collection for LOGP_WEIBULL_SP");
    }
  }

  // Legacy score types
  else if(score_type == LOGP_EXP_SP){
    if(!score_match_collection_logp_exp_sp(
          match_collection, top_rank_for_p_value)){
      carp(CARP_ERROR, "Failed to score match collection for LOGP_EXP_SP");
    }
  }else if(score_type == LOGP_BONF_EXP_SP){
    if(!score_match_collection_logp_bonf_exp_sp(
          match_collection, top_rank_for_p_value)){
      carp(CARP_ERROR,"Failed to score match collection for LOGP_BONF_EXP_SP");
    }
  }else if(score_type == LOGP_BONF_WEIBULL_SP){
    if(!score_match_collection_logp_bonf_weibull_sp(
          match_collection, top_rank_for_p_value)){
      carp(CARP_ERROR, 
          "failed to score match collection for LOGP_BONF_WEIBULL_SP");
    }
  }else if(//score_type == XCORR || // moved to above
          score_type == LOGP_BONF_EVD_XCORR || 
          score_type == LOGP_EVD_XCORR || 
          score_type == LOGP_BONF_WEIBULL_XCORR || 
          score_type == LOGP_WEIBULL_XCORR ){
    if(!score_match_collection_xcorr(match_collection, spectrum, charge)){
      carp(CARP_ERROR, 
      "Failed to score match collection for XCORR for spectrum %d, charge %d",
           get_spectrum_first_scan(spectrum), charge);
    }
  }else if(score_type == LOGP_BONF_EVD_XCORR){
    if(!score_match_collection_logp_bonf_evd_xcorr(match_collection, 
                                                   top_rank_for_p_value)){
      carp(CARP_ERROR, 
           "Failed to score match collection for LOGP_BONF_EVD_XCORR");
    }
  }else if(score_type == LOGP_EVD_XCORR){
    if(!score_match_collection_logp_evd_xcorr(match_collection, 
                                              top_rank_for_p_value)){
      carp(CARP_ERROR, "failed to score match collection for LOGP_EVD_XCORR");
    }
  }else if(score_type == LOGP_WEIBULL_XCORR){
    if(!score_match_collection_logp_weibull_xcorr(match_collection, 
                                                  top_rank_for_p_value)){
      carp(CARP_ERROR, 
           "failed to score match collection for LOGP_WEIBULL_XCORR");
    }
  }
  
  // free generate_peptides_iterator
  free_generate_peptides_iterator(peptide_iterator);

  carp(CARP_DETAILED_DEBUG, 
       "Finished creating match collection for spectrum %d, charge %d",
       get_spectrum_first_scan(spectrum), charge);  
  return match_collection;
}

/**
 * sort the match collection by score_type(SP, XCORR, ... )
 *\returns TRUE, if successfully sorts the match_collection
 */
BOOLEAN_T sort_match_collection(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to sort -out
  SCORER_TYPE_T score_type ///< the score type (SP, XCORR) to sort by -in
  )
{
  // check if we are allowed to alter match_collection
  if(match_collection->iterator_lock){
    carp(CARP_ERROR, 
         "Cannot alter match_collection when a match iterator is already"
         " instantiated");
    return FALSE;
  }

  switch(score_type){
  case DOTP:
    // implement later
    return FALSE;
  case XCORR:
  case LOGP_EVD_XCORR:
  case LOGP_BONF_EVD_XCORR:
  case LOGP_WEIBULL_XCORR: 
  case LOGP_BONF_WEIBULL_XCORR: 
    // LOGP_BONF_EVD_XCORR and XCORR have same order, 
    // sort the match to decreasing XCORR order for the return
    qsort_match(match_collection->match, match_collection->match_total, 
                (void *)compare_match_xcorr);
    match_collection->last_sorted = XCORR;
    return TRUE;
  case SP: 
  case LOGP_EXP_SP: 
  case LOGP_BONF_EXP_SP: 
  case LOGP_WEIBULL_SP: 
  case LOGP_BONF_WEIBULL_SP: 
  case LOGP_QVALUE_WEIBULL_XCORR: 
    // LOGP_EXP_SP and SP have same order, 
    // thus sort the match to decreasing SP order for the return
    carp(CARP_DEBUG, "Sorting match_collection %i", 
        match_collection->match_total);
    qsort_match(match_collection->match, 
        match_collection->match_total, (void *)compare_match_sp);
    carp(CARP_DEBUG, "Sorting match_collection %i", 
        match_collection->match_total);
    match_collection->last_sorted = SP;
    return TRUE;
  case Q_VALUE:
  case PERCOLATOR_SCORE:
    qsort_match(match_collection->match, match_collection->match_total, 
        (void *)compare_match_percolator_score);
    match_collection->last_sorted = PERCOLATOR_SCORE;
    return TRUE;
  }
  return FALSE;
}

/**
 * \brief Sort a match_collection by the given score type, grouping
 * matches by spectrum (if multiple spectra present).
 * \returns TRUE if sort is successful, else FALSE;
 */
BOOLEAN_T spectrum_sort_match_collection(
  MATCH_COLLECTION_T* match_collection, ///< match collection to sort -out
  SCORER_TYPE_T score_type ///< the score type to sort by -in
  ){

  BOOLEAN_T success = FALSE;

  // check if we are allowed to alter match_collection
  if(match_collection->iterator_lock){
    carp(CARP_ERROR, 
         "Cannot alter match_collection when a match iterator is already"
         " instantiated");
    return FALSE;
  }

  switch(score_type){
  case DOTP:
    success = FALSE;
    break;

  case XCORR:
  case LOGP_EVD_XCORR:
  case LOGP_BONF_EVD_XCORR:
  case LOGP_WEIBULL_XCORR: 
  case LOGP_BONF_WEIBULL_XCORR: 
    qsort_match(match_collection->match, match_collection->match_total,
                (void*)compare_match_spectrum_xcorr);
    match_collection->last_sorted = XCORR;
    success = TRUE;
    break;

  case SP: 
  case LOGP_EXP_SP: 
  case LOGP_BONF_EXP_SP: 
  case LOGP_WEIBULL_SP: 
  case LOGP_BONF_WEIBULL_SP: 
  case LOGP_QVALUE_WEIBULL_XCORR: 
    qsort_match(match_collection->match, match_collection->match_total,
                (void*)compare_match_spectrum_sp);
    match_collection->last_sorted = SP;
    success = TRUE;
    break;

  case Q_VALUE:
    qsort_match(match_collection->match, match_collection->match_total,
                (void*)compare_match_spectrum_q_value);
    match_collection->last_sorted = Q_VALUE;
    success = TRUE;
    break;

  case PERCOLATOR_SCORE:
    qsort_match(match_collection->match, match_collection->match_total,
                (void*)compare_match_spectrum_percolator_score);
    match_collection->last_sorted = PERCOLATOR_SCORE;
    success = TRUE;
    break;


  }

  return success;
}


/**
 * keeps the top max_rank number of matches and frees the rest
 * sorts by score_type(SP, XCORR, ...)
 */
void truncate_match_collection(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to truncate -out
  int max_rank,     ///< max number of top rank matches to keep from SP -in
  SCORER_TYPE_T score_type ///< the score type (SP, XCORR) -in
  )
{
  if (match_collection->match_total == 0){
    carp(CARP_DETAILED_INFO, "No matches in collection, so not truncating");
    return;
  }
  // sort match collection by score type
  // check if the match collection is in the correct sorted order
  if(match_collection->last_sorted != score_type){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, score_type)){
      die("Failed to sort match collection");
    }
  }

  // are there any matches to free?
  while(match_collection->match_total > max_rank){
    free_match(match_collection->match[match_collection->match_total - 1]);
    --match_collection->match_total;
  }
}

/**
 * Must provide a match_collection that is already scored, ranked for score_type
 * Rank 1, means highest score
 * \returns TRUE, if populates the match rank in the match collection
 */
BOOLEAN_T populate_match_rank_match_collection(
 MATCH_COLLECTION_T* match_collection, ///< the match collection to populate match rank -out
 SCORER_TYPE_T score_type ///< the score type (SP, XCORR) -in
 )
{
  // check if the match collection is in the correct sorted order
  if(match_collection->last_sorted != score_type){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, score_type)){
      carp(CARP_ERROR, "failed to sort match collection");
      return FALSE;
    }
  }

  // set match rank for all match objects
  int match_index;
  for(match_index=0; match_index<match_collection->match_total; ++match_index){
    set_match_rank(
        match_collection->match[match_index], score_type, match_index+1);
  }
  
  return TRUE;
}

/**
 * Create a new match_collection by randomly sampling matches 
 * from match_collection upto count number of matches
 * Must not free the matches
 * \returns a new match_collection of randomly sampled matches 
 */
MATCH_COLLECTION_T* random_sample_match_collection(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to sample -out
  int count_max ///< the number of matches to randomly select -in
  )
{
  int count_idx = 0;
  int match_idx = 0;
  int score_type_idx = 0;
  MATCH_COLLECTION_T* sample_collection = allocate_match_collection();
  srand(time(NULL));

  // make sure we don't sample more than the matches in the match collection
  if (count_max >= match_collection->match_total){
    free_match_collection(sample_collection);
    return match_collection;
  }

  // ranomly select matches upto count_max
  for(; count_idx < count_max; ++count_idx){
    match_idx = ((double)rand()/((double)RAND_MAX + (double)1)) 
      * match_collection->match_total;
    
    // match_idx = rand() % match_collection->match_total;
    sample_collection->match[count_idx] = match_collection->match[match_idx];
    // increment pointer count of the match object 
    increment_match_pointer_count(sample_collection->match[count_idx]);
  }
  
  // set total number of matches sampled
  sample_collection->match_total = count_idx;

  sample_collection->experiment_size = match_collection->experiment_size;

  // set scored types in the sampled matches
  for(; score_type_idx < _SCORE_TYPE_NUM;  ++score_type_idx){
    sample_collection->scored_type[score_type_idx] 
      = match_collection->scored_type[score_type_idx];
  }
  
  return sample_collection;
}

/**
 * This function is a transformation of the partial derivatives of
 * the log likelihood of the data given an extreme value distribution
 * with location parameter mu and scale parameter 1/L. The transformation 
 * has eliminated the explicit dependence on the location parameter, mu, 
 * leaving only the scale parameter, 1/L.
 *
 * The zero crossing of this function will correspond to the maximum of the 
 * log likelihood for the data.
 *
 * See equations 10 and 11 of "Maximum Likelihood fitting of extreme value 
 * distributions".
 *
 * The parameter values contains a list of the data values.
 * The parameter L is the reciprocal of the scale parameters.
 *
 *\returns the final exponential values of the score and sets the value of the function and its derivative.
 */
void constraint_function(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to estimate evd parameters -in
  SCORER_TYPE_T score_type, ///< score_type to estimate EVD distribution -in
  float l_value,  ///< L value -in
  float* function,  ///< the output function value -out
  float* derivative,  ///< the output derivative value -out
  float* exponential_sum ///< the final exponential array sum -out
  )
{
  int idx = 0;
  float* exponential = (float*)mycalloc(match_collection->match_total, sizeof(float));
  float numerator = 0;
  float second_numerator = 0;
  float score = 0;
  float denominator = 0;
  float score_sum = 0;
  MATCH_T** matches = match_collection->match;

  // iterate over the matches to calculate numerator, exponential value, denominator
  for(; idx < match_collection->match_total; ++idx){
    score = get_match_score(matches[idx], score_type);
    exponential[idx] = exp(-l_value * score);
    numerator += (exponential[idx] * score);
    denominator += exponential[idx];
    score_sum += score;
    second_numerator += (score * score * exponential[idx]);
  }

  // assign function value
  *function = (1.0 / l_value) - (score_sum / match_collection->match_total) 
    + (numerator / denominator);

  // assign derivative value
  *derivative =  ((numerator * numerator) / (denominator * denominator)) 
    - ((second_numerator / denominator)) - (1.0 / (l_value * l_value));

  // assign the total sum of the exponential values
  *exponential_sum = denominator;

  // free exponential array
  free(exponential);
}

/**
 * Randomly samples max_count peptides from the peptide distribution and try to 
 * estimate the Xcorr distribution of the the entire peptide distribution 
 * from the sampled peptide distribution. Populates the two EVD parameters mu, 
 * lambda in the match_collection.
 *
 * This function finds the location parameter, mu, and scale parameter, 1/L, 
 * that maximize the log likelihood of the data given an extreme value 
 * distribution.  It finds the parameters by using Newton-Raphson to find 
 * the zero of the constraint function.  The zero of the constraint function 
 * corresponds to the scale parameter giving the maximum log likelihood for the 
 * data.
 *
 * The parameter values contains the list of the data values.
 * The parameter starting_L contains a staring guess for L.
 * The parameter contains the tolerence for determining convergence.
 *
 * Returns the values of mu and L that maximize the log likelihood.
 * Throws an exception if Newton-Raphson fails to converge.
 * \returns TRUE, if successfully calculates the EVD parameters 
 */
BOOLEAN_T estimate_evd_parameters(
  MATCH_COLLECTION_T* match_collection, 
    ///< the match collection to estimate evd parameters -out
  int sample_count, 
    ///< the number of peptides to sample from the match_collection -in
  SCORER_TYPE_T score_type, 
    ///< score_type to estimate EVD distribution -in
  SPECTRUM_T* spectrum,    
    ///< the spectrum to score -in
  int charge       
    ///< the charge of the spectrum -in
  )
{
  // randomly sample from match collection
  MATCH_COLLECTION_T* sample_collection 
    = random_sample_match_collection(match_collection, sample_count);
  float l_value = 1;
  float f = 0.0;
  float f_prime = 0.0;
  float epsilon = 0.001;
  float exponential_sum = 0;
  int max_iterations = 10000;
  int idx = 0;

  // print info
  carp(CARP_INFO, "Estimate EVD parameters, sample count: %d", sample_count);
  
  // first score the sample match_collection
  if(score_type == XCORR){
    if(!score_match_collection_xcorr(sample_collection, spectrum, charge)){
      carp(CARP_ERROR, "failed to score match collection for XCORR");
    }
  }
  // FIXME Add different scoring if needed
  // ...

  // estimate the EVD parameters
  for(; idx < max_iterations; ++idx){
    constraint_function(sample_collection, score_type, l_value, 
                                       &f, &f_prime, &exponential_sum);

    if(fabsf(f) < epsilon){
      break;
    }
    else{
      l_value = l_value - f / f_prime;
    }
    
    // failed to converge error..
    if(idx >= max_iterations){
      carp(CARP_ERROR, "Root finding failed to converge.");
      free_match_collection(sample_collection);
      return FALSE;
    }
  }
  
  // Calculate best value of position parameter from best value of 
  // scale parameter.
  match_collection->mu 
    = -1.0 / l_value * logf(1.0 / sample_count * exponential_sum);
  match_collection->l_value = l_value;
    
  // free up sampled match_collection 
  free_match_collection(sample_collection);
  
  return TRUE;
}


// TODO score_match_collection_sp should probably not take an iterator?
// TODO change score_match_collection* to single routine score_match_collection
// TODO sample_count should probably not be an explicit parameter to the
// fitting code (should be like e.g. fraction-top-scores-to-fit)

/**
 * For the #top_count ranked peptides, calculate the Weibull parameters
 *\returns TRUE, if successfully calculates the Weibull parameters
 */
#define MIN_XCORR_SHIFT -5.0
#define MAX_XCORR_SHIFT  5.0
#define XCORR_SHIFT 0.05
#define MIN_SP_SHIFT -100.0
#define MAX_SP_SHIFT 300.0
#define SP_SHIFT 5.0

BOOLEAN_T estimate_weibull_parameters(
  MATCH_COLLECTION_T* match_collection, 
  ///< the match collection for which to estimate weibull parameters -out
  SCORER_TYPE_T score_type,
  int sample_count,
  SPECTRUM_T* spectrum,
  int charge
  )
{
  carp(CARP_DEBUG, "Estimating weibull params");
  MATCH_COLLECTION_T* sample_collection = match_collection;

  if (sample_count != 0){
    sample_collection = 
      random_sample_match_collection(match_collection, sample_count);
  }

  // how many things are we going to fit. We may want to just fit to the
  // tail, thus the distinction between total* and fit*
  int total_data_points = sample_collection->match_total;
  int fit_data_points = total_data_points;
  carp(CARP_DETAILED_DEBUG, "Stat: Total matches: %i\n", total_data_points);

  // for either param, 0 indicates use all peptides
  double fraction_to_fit = get_double_parameter("fraction-top-scores-to-fit");
  int number_to_fit = get_int_parameter("number-top-scores-to-fit");
  carp(CARP_DETAILED_DEBUG, "Number matches to fit %i, fraction to fit %f",
       number_to_fit, fraction_to_fit);

  //  if (fraction_to_fit > -0.5){
  if (fraction_to_fit > 0){
    assert(fraction_to_fit <= 1.0); // should have been checked in params
    fit_data_points = (int)(total_data_points * fraction_to_fit);
  }// else if (number_to_fit > -1 ){
  else if( number_to_fit > 0  ){
    //    fit_data_points = number_to_fit < total_data_points ? 
    //    number_to_fit : total_data_points;
    if( number_to_fit > total_data_points ){
      carp(CARP_WARNING, "Spectrum %i charge %i was not scored. Not " \
           "enough peptides to estimate distribution parameters. " \
           "(found %i, minimum %i)",
           get_spectrum_first_scan(spectrum), match_collection->charge,
           total_data_points, number_to_fit);
      return FALSE;
    }
    fit_data_points = number_to_fit;
  }

  carp(CARP_DETAILED_DEBUG, "Estimate Weibull parameters on %d scores",
       fit_data_points);
  
  // first score the sample match_collection
  if(score_type == XCORR){
    if(!score_match_collection_xcorr(sample_collection, spectrum, charge)){
      carp(CARP_ERROR, "Failed to score match collection for XCORR");
    }
    carp(CARP_DETAILED_DEBUG, "After scoring for xcorr collection is marked as scored? %i", sample_collection->scored_type[XCORR]);
  } else if (score_type == SP){
    // FIXME assumes scored by SP already
    ;
  }

  // check if the match collection is in the correct sorted order
  if(sample_collection->last_sorted != score_type){
    // sort match collection by score type
    if(!sort_match_collection(sample_collection, score_type)){
      free_match_collection(sample_collection);
      //die("Failed to sort match collection");
      carp(CARP_FATAL, "Failed to sort match collection");
      exit(1);
    }
  }

  // implementation of Weibull distribution parameter estimation from 
  // http:// www.chinarel.com/onlincebook/LifeDataWeb/rank_regression_on_y.htm
  
  int idx;
  float* data   = mycalloc(sizeof(float), total_data_points);
  for(idx=0; idx < total_data_points; idx++){
    float score = get_match_score(sample_collection->match[idx], score_type);
    data[idx] = score;
  }

  float correlation = 0.0;
  if (score_type == XCORR){
    fit_three_parameter_weibull(data, fit_data_points, total_data_points,
      MIN_XCORR_SHIFT, MAX_XCORR_SHIFT, XCORR_SHIFT, 
      &(match_collection->eta), &(match_collection->beta),
      &(match_collection->shift), &correlation);
  } else if (score_type == SP){
    fit_three_parameter_weibull(data, fit_data_points, total_data_points,
      MIN_SP_SHIFT, MAX_SP_SHIFT, SP_SHIFT, 
      &(match_collection->eta), &(match_collection->beta), 
      &(match_collection->shift), &correlation);
  }
  carp(CARP_DETAILED_DEBUG, 
      "Correlation: %.6f\nEta: %.6f\nBeta: %.6f\nShift: %.6f\n", 
      correlation, match_collection->eta, match_collection->beta,
      match_collection->shift);
  
  if (sample_collection != match_collection){ 
    // only free sample collection if it is not our original match collection
    free_match_collection(sample_collection);
  }
  free(data);
  return TRUE;
}


/**
 * For the #top_count SP ranked peptides, calculate the mean for which the
 * #top_ranked peptide score is set to 0, thus scaling the SP scores.
 *\returns TRUE, if successfully calculates the EXP_SP parameters
 */
BOOLEAN_T estimate_exp_sp_parameters(
  MATCH_COLLECTION_T* match_collection, 
    ///< the match collection to estimate evd parameters -out
  int top_count 
    ///< the number of top SP peptides to use for the match_collection -in
  )
{
  float top_sp_score = 0.0;
  float base_score = 0.0;
  int count  = 0;
  
  // sort match collection by SP
  // check if the match collection is in the correct sorted order
  if(match_collection->last_sorted != SP){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, SP)){
      carp(CARP_ERROR, "failed to sort match collection");
      exit(1);
    }
  }
  
  // adjust the number of top ranked peptides to sample
  // because the the total number of peptides are less than top_count
  if(top_count > match_collection->match_total){
    top_count = match_collection->match_total;
    carp(CARP_INFO, "");
  }
  
  // set the base score to which score is set to 0
  base_score = get_match_score(match_collection->match[top_count-1], SP);
  
  // compile the scores
  while(count < top_count){
    top_sp_score += get_match_score(match_collection->match[count], SP);
    ++count;
  }
  
  match_collection->sp_scores_mean = ((top_sp_score) / count - base_score);
  match_collection->base_score_sp = base_score;
  match_collection->top_fit_sp = top_count;
  
  return TRUE;
}

/**
 * Preliminary scoring methods
 *
 * Preliminary scoring methods create new match objects.
 * Also, they must set the match object null peptide boolean value.
 * To get the peptide sequence the order should always be
 * 1. get peptide object from generate_peptides or some other sources.
 * 2. create new match object.
 * 3. set match object null_peptide value.
 * 4. set peptide as match objects peptide.
 * 5. Finally get peptide sequence through get_match_sequence method
 */

/**
 * Preliminary scoring method: 
 * creates new match objects, and sets them as to which they are null 
 * peptide or not
 *
 * scores the match_collection, the score type SP
 * Assumes this is the first time scoring with this score_collection,
 * thus, prior number match object is 0.
 * the routine will use generate_peptides for each peptide will create a match
 * that maps the peptide to the spectrum.
 * If the score has already been computed simply returns TRUE 
 *\returns TRUE, if successfully populates the Sp scores in the match_collection
 */
BOOLEAN_T score_match_collection_sp(
  MATCH_COLLECTION_T* match_collection, 
    ///< the match collection to score -out
  SPECTRUM_T* spectrum, 
    ///< the spectrum to match peptides -in
  int charge,       
    ///< the charge of the spectrum -in
  GENERATE_PEPTIDES_ITERATOR_T* peptide_iterator 
    ///< the peptide iterator to score -in
  )
{
  
  // is this a empty collection?
  if(match_collection->match_total != 0){
    carp(CARP_ERROR, "must start with empty match collection");
    return FALSE;
  }
  
  // set ion constraint to sequest settings
  ION_CONSTRAINT_T* ion_constraint = 
    new_ion_constraint_sequest_sp(charge); 
  
  // create new scorer
  SCORER_T* scorer = new_scorer(SP);  

  char* peptide_sequence = NULL;
  MATCH_T* match = NULL;
  float score = 0;
  PEPTIDE_T* peptide = NULL;  

  // create a generic ion_series that will be reused for each peptide sequence
  ION_SERIES_T* ion_series = new_ion_series_generic(ion_constraint, charge);  
  
  // iterate over all peptides
  carp(CARP_DEBUG, "Iterating over peptides to score Sp");
  while(generate_peptides_iterator_has_next(peptide_iterator)){
    peptide = generate_peptides_iterator_next(peptide_iterator);

    // create a new match
    match = new_match();

    // set match if it is to be generated as null peptide match
    set_match_null_peptide(match, match_collection->null_peptide_collection);
    
    // now set peptide and spectrum
    set_match_peptide(match, peptide);

    set_match_spectrum(match, spectrum);
    
    // get peptide sequence    
    peptide_sequence = get_match_sequence(match);
    
    // update ion_series for the peptide instance    
    update_ion_series(ion_series, peptide_sequence);

    // now predict ions for this peptide
    predict_ions(ion_series);
    
    // calculates the Sp score
    score = score_spectrum_v_ion_series(scorer, spectrum, ion_series);

    // increment the total sp score
    match_collection->sp_scores_mean += score;
        
    // set score in match
    set_match_score(match, SP, score);

    // set b_y_ion_match field
    set_match_b_y_ion_info(match, scorer);
    
    // check if enough space for peptide match
    if(match_collection->match_total >= _MAX_NUMBER_PEPTIDES){
      carp(CARP_ERROR, "peptide count of %i exceeds max match limit: %d", 
          match_collection->match_total, _MAX_NUMBER_PEPTIDES);
      // free heap
      free(peptide_sequence);
      free_ion_series(ion_series);
      free_scorer(scorer);
      free_ion_constraint(ion_constraint);

      return FALSE;
    }
    
    // add a new match to array
    match_collection->match[match_collection->match_total] = match;
    
    // increment total match count
    ++match_collection->match_total;

    // print total peptides scored so far
    if(match_collection->match_total % 10000 == 0){
      carp(CARP_DEBUG, "scored peptide for sp: %d", 
          match_collection->match_total);
    }
    
    free(peptide_sequence);
  }
  // free ion_series now that we are done iterating over all peptides
  free_ion_series(ion_series);
  
  // calculate the final sp score mean
  match_collection->sp_scores_mean /= match_collection->match_total;
  
  // total peptide experiment sample size
  match_collection->experiment_size = match_collection->match_total;

  // print total peptides scored so far
  carp(CARP_DEBUG, "Total peptide scored for sp: %d", 
      match_collection->match_total);

  //if (match_collection->match_total)
  
  // free heap
  free_scorer(scorer);
  free_ion_constraint(ion_constraint);
    
  // now match_collection is sorted, populate the rank of each match object
  if(!populate_match_rank_match_collection(match_collection, SP)){
    carp(CARP_ERROR, "failed to populate rank for SP in match_collection");
    free_match_collection(match_collection);
    exit(1);
  }
  
  // yes, we have now scored for the match-mode: SP
  match_collection->scored_type[SP] = TRUE;
  
  return TRUE;
}


/**
 * Main scoring methods
 * 
 * Unlike preliminary scoring methods only updates existing mathc objects 
 * with new scores. In all cases should get peptide sequence only through 
 * get_match_sequence method.
 */


/**
 * The match collection must be scored under SP first
 * \returns TRUE, if successfully scores matches for LOGP_EXP_SP
 */
BOOLEAN_T score_match_collection_logp_exp_sp(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to score -out
  int peptide_to_score ///< the number of top ranked sp scored peptides to score for logp_exp_sp -in
  )
{
  int match_idx = 0;
  float score = 0;
  MATCH_T* match = NULL;
  
  // has the score type been populated in match collection?
  if(!match_collection->scored_type[SP]){
    carp(CARP_ERROR, "the collection must be scored by SP first before LOGP_EXP_SP");
    exit(1);
  }

  // sort by SP if not already sorted.
  // This enables to identify the top ranked SP scoring peptides
  if(match_collection->last_sorted != SP){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, SP)){
      carp(CARP_ERROR, "failed to sort match collection by SP");
      free_match_collection(match_collection);
      exit(1);
    }
  }
  
  // we are string xcorr!
  carp(CARP_DEBUG, "start scoring for LOGP_EXP_SP");

  // iterate over all matches to score for LOGP_EXP_SP
  while(match_idx < match_collection->match_total && match_idx < peptide_to_score){
    match = match_collection->match[match_idx];
    // scale the SP score by the base score found from estimate_exp_sp_parameters routine
    score = score_logp_exp_sp((get_match_score(match, SP) - match_collection->base_score_sp), match_collection->sp_scores_mean);
    
    // set all fields in match
    set_match_score(match, LOGP_EXP_SP, score);
    ++match_idx;
  }
  
  // we are done
  carp(CARP_INFO, "total peptides scored for LOGP_EXP_SP: %d", match_idx);

  // match_collection is not populate with the rank of LOGP_EXP_SP, becuase the SP rank is  identical to the LOGP_EXP_SP rank
  
  // yes, we have now scored for the match-mode: LOGP_EXP_SP
  match_collection->scored_type[LOGP_EXP_SP] = TRUE;
  
  return TRUE;
}


/**
 * The match collection must be scored under SP first
 * \returns TRUE, if successfully scores matches for LOGP_BONF_EXP_SP
 */
BOOLEAN_T score_match_collection_logp_bonf_exp_sp(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to score -out
  int peptide_to_score ///< the number of top ranked sp scored peptides to score for logp_bonf_exp_sp -in
  )
{
  int match_idx = 0;
  double score = 0;
  MATCH_T* match = NULL;
  
  // has the score type been populated in match collection?
  if(!match_collection->scored_type[SP]){
    carp(CARP_ERROR, "the collection must be scored by SP first before LOGP_EXP_SP");
    exit(1);
  }

  // sort by SP if not already sorted.
  // This enables to identify the top ranked SP scoring peptides
  if(match_collection->last_sorted != SP){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, SP)){
      carp(CARP_ERROR, "failed to sort match collection by SP");
      free_match_collection(match_collection);
      exit(1);
    }
  }
  
  // we are string xcorr!
  carp(CARP_DEBUG, "start scoring for LOGP_BONF_EXP_SP");

  // iterate over all matches to score for LOGP_BONF_EXP_SP
  while(match_idx < match_collection->match_total && match_idx < peptide_to_score){
    match = match_collection->match[match_idx];
    // scale the SP score by the base score found from estimate_exp_sp_parameters routine
    score = score_logp_bonf_exp_sp((get_match_score(match, SP) - match_collection->base_score_sp), match_collection->sp_scores_mean, match_collection->experiment_size);
    
    // set all fields in match
    set_match_score(match, LOGP_BONF_EXP_SP, score);
    ++match_idx;
  }
  
  // we are done
  carp(CARP_DEBUG, "total peptides scored for LOGP_BONF_EXP_SP: %d", match_idx);
    
  // match_collection is not populate with the rank of LOGP_BONF_EXP_SP, becuase the SP rank is  identical to the LOGP_EXP_SP rank
  
  // yes, we have now scored for the match-mode: LOGP_BONF_EXP_SP
  match_collection->scored_type[LOGP_BONF_EXP_SP] = TRUE;
  
  return TRUE;
}

/**
 * The match collection must be scored under SP first
 * \returns TRUE, if successfully scores matches for LOGP_WEIBULL_SP
 */
BOOLEAN_T score_match_collection_logp_weibull_sp(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to score -out
  int peptide_to_score ///< the number of top ranked sp scored peptides to score for logp_weibull_sp -in
  )
{
  int match_idx = 0;
  float score = 0;
  MATCH_T* match = NULL;
  
  // has the score type been populated in match collection?
  if(!match_collection->scored_type[SP]){
    carp(CARP_ERROR, "the collection must be scored by SP first before LOGP_WEIBULL_SP");
    exit(1);
  }

  // sort by SP if not already sorted.
  // This enables to identify the top ranked SP scoring peptides
  if(match_collection->last_sorted != SP){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, SP)){
      carp(CARP_ERROR, "failed to sort match collection by SP");
      free_match_collection(match_collection);
      exit(1);
    }
  }
  
  // we are string xcorr!
  carp(CARP_DEBUG, "start scoring for LOGP_WEIBULL_SP");

  // iterate over all matches to score for LOGP_WEIBULL_SP
  while(match_idx < match_collection->match_total && match_idx < peptide_to_score){
    match = match_collection->match[match_idx];
    // scale the SP score by the base score found from estimate_weibull_sp_parameters routine
    score = score_logp_weibull(get_match_score(match, SP),
          match_collection->eta, match_collection->beta);
    
    // set all fields in match
    set_match_score(match, LOGP_WEIBULL_SP, score);
    ++match_idx;
  }
  
  // we are done
  carp(CARP_DEBUG, "total peptides scored for LOGP_WEIBULL_SP: %d", match_idx);

  // match_collection is not populate with the rank of LOGP_WEIBULL_SP, becuase the SP rank is  identical to the LOGP_WEIBULL_SP rank
  
  // yes, we have now scored for the match-mode: LOGP_WEIBULL_SP
  match_collection->scored_type[LOGP_WEIBULL_SP] = TRUE;
  
  return TRUE;
}

/**
 * The match collection must be scored under XCORR first
 * \returns TRUE, if successfully scores matches for LOGP_WEIBULL_XCORR
 */
BOOLEAN_T score_match_collection_logp_weibull_xcorr(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to score -out
  int peptide_to_score ///< the number of top ranked xcorr scored peptides to score for logp_weibull_xcorr -in
  )
{
  int match_idx = 0;
  float score = 0;
  MATCH_T* match = NULL;
  
  // has the score type been populated in match collection?
  if(!match_collection->scored_type[XCORR]){
    carp(CARP_ERROR, "the collection must be scored by XCORR first before LOGP_WEIBULL_XCORR");
    exit(1);
  }

  // sort by XCORR if not already sorted.
  // This enables to identify the top ranked XCORR scoring peptides
  if(match_collection->last_sorted != XCORR){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, XCORR)){
      carp(CARP_ERROR, "failed to sort match collection by XCORR");
      free_match_collection(match_collection);
      exit(1);
    }
  }
  
  // we are string xcorr!
  carp(CARP_DEBUG, "start scoring for LOGP_WEIBULL_XCORR");

  // iterate over all matches to score for LOGP_WEIBULL_XCORR
  while(match_idx < match_collection->match_total && match_idx < peptide_to_score){
    match = match_collection->match[match_idx];
    // scale the XCORR score by the base score found from estimate_weibull_parameters routine
    score = score_logp_weibull(get_match_score(match, XCORR),
          match_collection->eta, match_collection->beta);
    // set all fields in match
    set_match_score(match, LOGP_WEIBULL_XCORR, score);
    ++match_idx;
  }
  
  // we are done
  carp(CARP_DEBUG, "total peptides scored for LOGP_WEIBULL_XCORR: %d", match_idx);

  // match_collection is not populate with the rank of LOGP_WEIBULL_XCORR, becuase the XCORR rank is  identical to the LOGP_WEIBULL_XCORR rank
  
  // yes, we have now scored for the match-mode: LOGP_WEIBULL_XCORR
  match_collection->scored_type[LOGP_WEIBULL_XCORR] = TRUE;
  
  return TRUE;
}


/**
 * Calculates a p-value for each psm in the collection based on the
 * estimated paramters of the weibull distribution (eta, beta, shift).
 *
 * P-value score is stored at index LOGP_BONF_WEIBULL_XCORR.  The
 * match collection must have been scored for XCORR first and the
 * parameters estimated before the collection was truncaged.
 * \returns TRUE, if successfully scores matches for LOGP_BONF_WEIBULL_XCORR
 */
BOOLEAN_T score_match_collection_logp_bonf_weibull_xcorr(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to score -out
  int peptide_to_score ///< the number of top ranked xcorr scored peptides to score for logp_bonf_weibull_xcorr -in
  )
{
  int match_idx = 0;
  double score = 0;
  MATCH_T* match = NULL;
  // score as many psms as will be printed to file
  int for_sqt = get_int_parameter("max-sqt-result");
  int for_csm =  get_int_parameter("top-match");
  peptide_to_score = (for_sqt > for_csm) ? for_sqt : for_csm;
  
  // has the score type been populated in match collection?
  if(!match_collection->scored_type[XCORR]){
    carp(CARP_FATAL, 
         "The matches must be scored by XCORR before calculating a p-value");
    exit(1);
  }

  // sort by XCORR to find the top ranked XCORR scoring peptides
  if(match_collection->last_sorted != XCORR){
    if(!sort_match_collection(match_collection, XCORR)){
      carp(CARP_FATAL, "Failed to sort match collection by XCORR");
      free_match_collection(match_collection);
      exit(1);
    }
  }
  
  carp(CARP_DETAILED_DEBUG, "start scoring for LOGP_BONF_WEIBULL_XCORR");

  // iterate over all matches to score for LOGP_BONF_WEIBULL_XCORR
  while(match_idx < match_collection->match_total && 
        match_idx < peptide_to_score){
    match = match_collection->match[match_idx];
    // scale the XCORR score by the params from estimate_weibull_parameters
    score = score_logp_bonf_weibull(get_match_score(match, XCORR),
          match_collection->eta, match_collection->beta, 
          match_collection->shift, match_collection->experiment_size);
    
    // set all fields in match
    set_match_score(match, LOGP_BONF_WEIBULL_XCORR, score);
    ++match_idx;

    carp(CARP_DETAILED_DEBUG, "index %i xrank %i xcorr %.2f p-val %f",
         match_idx, get_match_rank(match, XCORR),
         get_match_score(match, XCORR), score);
  }
  
  // we are done
  carp(CARP_DEBUG, 
       "Total peptides scored for LOGP_BONF_WEIBULL_XCORR: %d", match_idx);
    
  // match_collection is not populated with the rank of
  // LOGP_BONF_WEIBULL_XCORR, becuase the XCORR rank is identical to
  // the LOGP_WEIBULL_XCORR rank
  // BF: but we will rank it anyway, becuase it makes it printing to
  // the sqt file easier
  populate_match_rank_match_collection(match_collection,
                                       LOGP_BONF_WEIBULL_XCORR);
  
  // yes, we have now scored for the match-mode: LOGP_BONF_WEIBULL_XCORR
  match_collection->scored_type[LOGP_BONF_WEIBULL_XCORR] = TRUE;
  
  return TRUE;
}



/**
 * The match collection must be scored under SP first
 * \returns TRUE, if successfully scores matches for LOGP_BONF_WEIBULL_SP
 */
BOOLEAN_T score_match_collection_logp_bonf_weibull_sp(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to score -out
  int peptide_to_score ///< the number of top ranked sp scored peptides to score for logp_bonf_weibull_sp -in
  )
{
  int match_idx = 0;
  float score = 0;
  MATCH_T* match = NULL;
  
  // has the score type been populated in match collection?
  if(!match_collection->scored_type[SP]){
    carp(CARP_ERROR, "the collection must be scored by SP first before LOGP_WEIBULL_SP");
    exit(1);
  }

  // sort by SP if not already sorted.
  // This enables to identify the top ranked SP scoring peptides
  if(match_collection->last_sorted != SP){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, SP)){
      carp(CARP_ERROR, "failed to sort match collection by SP");
      free_match_collection(match_collection);
      exit(1);
    }
  }
  
  // we are string xcorr!
  carp(CARP_DEBUG, "start scoring for LOGP_BONF_WEIBULL_SP");

  // iterate over all matches to score for LOGP_BONF_WEIBULL_SP
  while(match_idx < match_collection->match_total && match_idx < peptide_to_score){
    match = match_collection->match[match_idx];
    // scale the SP score by the params from estimate_weibull_sp_parameters
    score = score_logp_bonf_weibull(get_match_score(match, SP),
          match_collection->eta, match_collection->beta, 
          match_collection->shift, match_collection->experiment_size);
    
    // set all fields in match
    set_match_score(match, LOGP_BONF_WEIBULL_SP, score);
    ++match_idx;
  }
  
  // we are done
  carp(CARP_DEBUG, "total peptides scored for LOGP_BONF_WEIBULL_SP: %d", match_idx);
    
  // match_collection is not populate with the rank of LOGP_BONF_WEIBULL_SP, becuase the SP rank is  identical to the LOGP_WEIBULL_SP rank
  
  // yes, we have now scored for the match-mode: LOGP_BONF_WEIBULL_SP
  match_collection->scored_type[LOGP_BONF_WEIBULL_SP] = TRUE;
  
  return TRUE;
}


/**
 * Assumes that match collection was scored under SP first
 * Creates an ion constraint, a scorer, an ion series.  Modifies the
 * matches in the collection by setting the score.
 * \returns TRUE, if successfully scores matches for xcorr
 * \callgraph
 */
BOOLEAN_T score_match_collection_xcorr(
  MATCH_COLLECTION_T* match_collection, ///<the match collection to score -out
  SPECTRUM_T* spectrum, ///< the spectrum to match peptides -in
  int charge       ///< the charge of the spectrum -in
  )
{
  MATCH_T* match = NULL;
  char* peptide_sequence = NULL;  
  float score = 0;
  
  /*
  // is this a empty collection?
  if(match_collection->match_total > 0){
    carp(CARP_ERROR, "must start with SP scored match collection");
    return FALSE;
  }
  */
  
  // set ion constraint to sequest settings
  ION_CONSTRAINT_T* ion_constraint = 
    new_ion_constraint_sequest_xcorr(charge); 
  
  // create new scorer
  SCORER_T* scorer = new_scorer(XCORR);  

  // create a generic ion_series that will be reused for each peptide sequence
  ION_SERIES_T* ion_series = new_ion_series_generic(ion_constraint, charge);  
  
  // we are scoring xcorr!
  carp(CARP_DEBUG, "Start scoring for XCORR");

  // iterate over all matches to score for xcorr
  int match_idx;
  for(match_idx=0; match_idx < match_collection->match_total; ++match_idx){
    match = match_collection->match[match_idx];
    peptide_sequence = get_match_sequence(match);
    
    // update ion_series for the peptide instance    
    update_ion_series(ion_series, peptide_sequence);
    
    // now predict ions
    predict_ions(ion_series);
    
    // calculates the Xcorr score
    score = score_spectrum_v_ion_series(scorer, spectrum, ion_series);
    char* decoy = "target";
    if (get_match_null_peptide(match)==TRUE){
      decoy = "decoy";
    }
    carp(CARP_DETAILED_DEBUG, "Spectrum %i vs. %s peptide %s = %.6f", 
      get_spectrum_first_scan(spectrum), decoy, peptide_sequence, score);

    // set all fields in match
    set_match_score(match, XCORR, score);
    
    // free heap
    free(peptide_sequence);   
  }  

  // free ion_series now that we are done iterating over all peptides
  free_ion_series(ion_series);

  // we scored xcorr!
  carp(CARP_DEBUG, "Total peptides scored for XCORR: %d", match_idx);

  // free heap
  free_scorer(scorer);
  free_ion_constraint(ion_constraint);

  // sort match collection by score type
  if(!sort_match_collection(match_collection, XCORR)){
    carp(CARP_FATAL, "Failed to sort match collection by Xcorr");
    exit(1);
  }
  
  // now the match_collection is sorted, update the rank of each match object
  if(!populate_match_rank_match_collection(match_collection, XCORR)){
    carp(CARP_FATAL, "Failed to populate match rank in match_collection");
    free_match_collection(match_collection);
    exit(1);
  }

  // calculate deltaCn value (difference between best and 2nd best score)
  if(match_collection->match_total > 1){
    match_collection->delta_cn = 
      get_match_score(match_collection->match[0], XCORR) -
      get_match_score(match_collection->match[1], XCORR);
  }
  else{
    // set to very small number
    match_collection->delta_cn = 0.000001;
  }
  
  // yes, we have now scored for the match-mode: XCORR
  match_collection->scored_type[XCORR] = TRUE;

  return TRUE;
}


/**
 * The match collection must be scored under Xcorr first
 * \returns TRUE, if successfully scores matches for LOGP_EVD_XCORR
 */
BOOLEAN_T score_match_collection_logp_evd_xcorr(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to score -out
  int peptide_to_score ///< the number of top ranked xcorr scored peptides to score for logp_evd_xcorr -in
  )
{
  int match_idx = 0;
  float score = 0;
  MATCH_T* match = NULL;
  
  // has the score type been populated in match collection?
  if(!match_collection->scored_type[XCORR]){
    carp(CARP_ERROR, "the collection must be scored by XCORR first before LOGP_EVD_XCORR");
    exit(1);
  }

  // sort by XCORR if not already sorted.
  // This enables to identify the top ranked XCORR scoring peptides
  if(match_collection->last_sorted != XCORR){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, XCORR)){
      carp(CARP_ERROR, "failed to sort match collection by XCORR");
      free_match_collection(match_collection);
      exit(1);
    }
  }
  
  // we are starting LOGP_EVD_XCORR!
  carp(CARP_DEBUG, "start scoring for LOGP_EVD_XCORR");

  // iterate over all matches to score for LOGP_EVD_XCORR
  while(match_idx < match_collection->match_total && match_idx < peptide_to_score){
    match = match_collection->match[match_idx];
    score = score_logp_evd_xcorr(get_match_score(match, XCORR), match_collection->mu, match_collection->l_value);
    
    // set all fields in match
    set_match_score(match, LOGP_EVD_XCORR, score);
    ++match_idx;
  }
  
  // we are done
  carp(CARP_DEBUG, "total peptides scored for LOGP_EVD_XCORR: %d", match_idx);

  // match_collection is not populate with the rank of LOGP_EVD_XCORR, 
  // becuase the XCORR rank is  identical to the LOGP_EVD_XCORR rank
  
  // yes, we have now scored for the match-mode: LOGP_EVD_XCORR
  match_collection->scored_type[LOGP_EVD_XCORR] = TRUE;
  
  return TRUE;
}

/**
 * The match collection must be scored under Xcorr first
 * \returns TRUE, if successfully scores matches for LOGP_BONF_EVD_XCORR
 */
BOOLEAN_T score_match_collection_logp_bonf_evd_xcorr(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to score -out
  int peptide_to_score ///< the number of top ranked xcorr scored peptides to score for logp_bonf_evd_xcorr -in
  )
{
  int match_idx = 0;
  float score = 0;
  MATCH_T* match = NULL;
  
  // has the score type been populated in match collection?
  if(!match_collection->scored_type[XCORR]){
    carp(CARP_ERROR, "the collection must be scored by XCORR first before LOGP_BONF_EVD_XCORR");
    exit(1);
  }

  // sort by XCORR if not already sorted.
  // This enables to identify the top ranked XCORR scoring peptides
  if(match_collection->last_sorted != XCORR){
    // sort match collection by score type
    if(!sort_match_collection(match_collection, XCORR)){
      carp(CARP_ERROR, "failed to sort match collection by XCORR");
      free_match_collection(match_collection);
      exit(1);
    }
  }
  
  // we are starting LOGP_BONF_EVD_XCORR!
  carp(CARP_DEBUG, "start scoring for LOGP_BONF_EVD_XCORR");

  // iterate over all matches to score for LOGP_BONF_EVD_XCORR
  while(match_idx < match_collection->match_total && match_idx < peptide_to_score){
    match = match_collection->match[match_idx];
    score = score_logp_bonf_evd_xcorr(get_match_score(match, XCORR), match_collection->mu, match_collection->l_value, match_collection->experiment_size);
    
    // set all fields in match
    set_match_score(match, LOGP_BONF_EVD_XCORR, score);
    ++match_idx;
  }
  
  // we are done
  carp(CARP_DEBUG, "total peptides scored for LOGP_BONF_EVD_XCORR: %d", match_idx);

  // match_collection is not populate with the rank of LOGP_BONF_EVD_XCORR, 
  // becuase the XCORR rank is  identical to the LOGP_BONF_EVD_XCORR rank
  
  // yes, we have now scored for the match-mode: LOGP_BONF_EVD_XCORR
  match_collection->scored_type[LOGP_BONF_EVD_XCORR] = TRUE;
  
  return TRUE;
}

/**
 * match_collection get, set method
 */

/**
 *\returns TRUE, if the match collection has been scored by score_type
 */
BOOLEAN_T get_match_collection_scored_type(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to iterate -in
  SCORER_TYPE_T score_type ///< the score_type (MATCH_SP, MATCH_XCORR) -in
  )
{
  return match_collection->scored_type[score_type];
}

/**
 * sets the score_type to value
 */
void set_match_collection_scored_type(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to iterate -in
  SCORER_TYPE_T score_type, ///< the score_type (MATCH_SP, MATCH_XCORR) -in
  BOOLEAN_T value
  )
{
  match_collection->scored_type[score_type] = value;
}

/**
 *\returns TRUE, if there is a  match_iterators instantiated by match collection 
 */
BOOLEAN_T get_match_collection_iterator_lock(
  MATCH_COLLECTION_T* match_collection ///< working match collection -in
  )
{
  return match_collection->iterator_lock;
}

/**
 *\returns the total match objects avaliable in current match_collection
 */
int get_match_collection_match_total(
  MATCH_COLLECTION_T* match_collection ///< working match collection -in
  )
{
  return match_collection->match_total;
}

/**
 *\returns the total peptides searched in the experiment in match_collection
 */
int get_match_collection_experimental_size(
  MATCH_COLLECTION_T* match_collection ///< working match collection -in
  )
{
  return match_collection->experiment_size;
}

/**
 *\returns the top peptide count used in the logp_exp_sp in match_collection
 */
int get_match_collection_top_fit_sp(
  MATCH_COLLECTION_T* match_collection ///< working match collection -in
  )
{
  return match_collection->top_fit_sp;
}

/**
 *\returns the charge of the spectrum that the match collection was created
 */
int get_match_collection_charge(
  MATCH_COLLECTION_T* match_collection ///< working match collection -in
  )
{
  return match_collection->charge;
}

/**
 * Must have been scored by Xcorr, returns error if not scored by Xcorr
 *\returns the delta cn value(difference in top and second ranked Xcorr values)
 */
float get_match_collection_delta_cn(
  MATCH_COLLECTION_T* match_collection ///< working match collection -in
  )
{
  // Check if xcorr value has been scored, thus delta cn value is valid
  if(match_collection->scored_type[XCORR]){
    return match_collection->delta_cn;
  }
  else{
    carp(CARP_ERROR, "must score match_collection with XCORR to get delta cn value");
    return 0.0;
  }
}

/**
 * \brief Names and opens the correct number of binary psm files.
 *
 * Takes the values of match-output-folder, ms2 filename (soon to be
 * named output file), overwrite, and number-decoy-set from parameter.c.
 * Exits with error if can't create new requested directory or if
 * can't create any of the psm files.
 * REPLACES: spectrum_collection::get_spectrum_collection_psm_result_filenames
 *
 * \returns An array of filehandles to the newly opened files
 */
FILE** create_psm_files(){

  int decoy_sets = get_int_parameter("number-decoy-set");
  int total_files = decoy_sets +1;
  // create FILE* array to return
  FILE** file_handle_array = (FILE**)mycalloc(total_files, sizeof(FILE*));
  int file_idx = 0;

  // Create null pointers if no binary output called for
  if( SQT_OUTPUT == get_output_type_parameter("output-mode") ){
    carp(CARP_DEBUG, "SQT mode: return empty array of file handles");
    return file_handle_array;
  }

  carp(CARP_DEBUG, "Opening %d new psm files", total_files);

  char* output_directory =get_string_parameter_pointer("match-output-folder");

  // create the output folder if it doesn't exist
  if(access(output_directory, F_OK)){
    if(mkdir(output_directory, S_IRWXU+S_IRWXG+S_IRWXO) != 0){
      carp(CARP_FATAL, "Failed to create output directory %s", 
           output_directory);
      exit(1);
    }
  }

  // get ms2 file for naming result file
  //TODO change to output filename as argument, force .csm extension
  //     add _decoy1.csm
  //char* base_filename = get_string_parameter_pointer("ms2 file");
  char* ms2_filename = get_string_parameter_pointer("ms2 file");
  //char** filename_path_array = parse_filename_path(base_filename);
  char** filename_path_array = 
    parse_filename_path_extension(ms2_filename, ".ms2");
  if( filename_path_array[1] == NULL ){
    filename_path_array[1] = ".";
  }

  carp(CARP_DEBUG, "Base filename is %s and path is %s", 
       filename_path_array[0], filename_path_array[1]);

  char* filename_template = get_full_filename(output_directory, 
                                              filename_path_array[0]);

  //create target file
  //  int temp_file_descriptor = -1;
  //  char suffix[25];
  BOOLEAN_T overwrite = get_boolean_parameter("overwrite");
  //first file is target, remaining are decoys
  //  char* psm_filename = generate_name(filename_template, "_XXXXXX", 
  //                                     ".ms2", "crux_match_target_");

  for(file_idx=0; file_idx<total_files; file_idx++){

    char* psm_filename = generate_psm_filename(filename_path_array[0], 
                                               file_idx);

    //get file descriptor for uniq version of name //TODO remove this
    //    temp_file_descriptor = mkstemp(psm_filename);
    //open the file from the descriptor
    //    file_handle_array[file_idx] = fdopen(temp_file_descriptor, "w");
    file_handle_array[file_idx] = create_file_in_path(psm_filename,
                                                      output_directory,
                                                      overwrite); 
    //check for error
    if( file_handle_array[file_idx] == NULL ){//||
      //temp_file_descriptor == -1 ){

      carp(CARP_FATAL, "Could not create psm file %s", psm_filename);
      exit(1);
    }
    //rename this, just for a quick fix
    free(filename_template);
    filename_template = get_full_filename(output_directory, psm_filename);
    //chmod(psm_filename, 0664);
    chmod(filename_template, 0664);

    //get next decoy name
    free(psm_filename);
    //sprintf(suffix, "crux_match_decoy_%d_", file_idx+1);
    //psm_filename = generate_name(filename_template, "_XXXXXX",
    //                             ".ms2", suffix);
  }

  // clean up 
  free(filename_path_array[0]);
  if( *filename_path_array[1] != '.' ){
    free(filename_path_array[1]);
  }
  free(filename_path_array);
  free(filename_template);

  return file_handle_array;

}

/**
 * \brief Serialize the PSM features to output file up to 'top_match'
 * number of top peptides from the match_collection.
 *
 * \details  First serialize the spectrum info of the match collection
 * then  iterate over matches and serialize the structs
 *
 * <int: charge state of the spectrum>
 * <int: Total match objects in the match_collection>
 * <float: delta_cn>
 * <float: ln_delta_cn>
 * <float: ln_experiment_size>
 * <BOOLEAN_T: had the score type been scored?>* - for all score types
 * <MATCH: serialized match struct>* <--serialize top_match match structs 
 *
 * \returns TRUE, if sucessfully serializes the PSMs, else FALSE 
 * \callgraph
 */
BOOLEAN_T serialize_psm_features(
  MATCH_COLLECTION_T* match_collection, ///< working match collection -in
  FILE* output,               ///< output file handle -out
  int top_match,              ///< number of top match to serialize -in
  SCORER_TYPE_T prelim_score, ///< the preliminary score to report -in
  SCORER_TYPE_T main_score    ///<  the main score to report -in
  )
{
  MATCH_T* match = NULL;
  
  // create match iterator 
  // TRUE tells iterator to return matches in sorted order of main_score type
  MATCH_ITERATOR_T* match_iterator = 
    new_match_iterator(match_collection, main_score, TRUE);
  
  float delta_cn =  get_match_collection_delta_cn(match_collection);
  float ln_delta_cn = logf(delta_cn);
  float ln_experiment_size = logf(match_collection->experiment_size);

  // spectrum specific features
  // first, serialize the spectrum info of the match collection  
  // the charge of the spectrum
  
  myfwrite(&(match_collection->charge), sizeof(int), 1, output); 
  myfwrite(&(match_collection->match_total), sizeof(int), 1, output);
  myfwrite(&delta_cn, sizeof(float), 1, output);
  myfwrite(&ln_delta_cn, sizeof(float), 1, output);
  myfwrite(&ln_experiment_size, sizeof(float), 1, output);
  
  // serialize each boolean for scored type 
  int score_type_idx;
  for(score_type_idx=0; score_type_idx < _SCORE_TYPE_NUM; ++score_type_idx){
    myfwrite(&(match_collection->scored_type[score_type_idx]), 
        sizeof(BOOLEAN_T), 1, output);
  }
  
  // second, iterate over matches and serialize them
  int match_count = 0;
  while(match_iterator_has_next(match_iterator)){
    ++match_count;
    match = match_iterator_next(match_iterator);        
    
    // FIXME
    prelim_score = prelim_score;
    
    // serialize matches
    serialize_match(match, output); // FIXME main, preliminary type
    
    // print only up to max_rank_result of the matches
    if(match_count >= top_match){
      break;
    }
  }
  
  free_match_iterator(match_iterator);
  
  return TRUE;
}

void print_sqt_header(
 FILE* output, 
 char* type, 
 int num_proteins, 
 BOOLEAN_T is_analysis){  // for analyze-matches look at algorithm param
  if( output == NULL ){
    return;
  }

  time_t hold_time;
  hold_time = time(0);

  BOOLEAN_T decoy = FALSE;
  if( strcmp(type, "decoy") == 0 ){
    decoy = TRUE;
  }

  fprintf(output, "H\tSQTGenerator Crux\n");
  fprintf(output, "H\tSQTGeneratorVersion 1.0\n");
  fprintf(output, "H\tComment Crux was written by...\n");
  fprintf(output, "H\tComment ref...\n");
  fprintf(output, "H\tStartTime\t%s", ctime(&hold_time));
  fprintf(output, "H\tEndTime                               \n");

  char* database = get_string_parameter("protein input");
  //  fprintf(output, "H\tDatabase\t%s\n", database);

  if( get_boolean_parameter("use-index") == TRUE ){
    char* fasta_name  = get_index_binary_fasta_name(database);
    free(database);
    database = fasta_name;
  }
  fprintf(output, "H\tDatabase\t%s\n", database);
  free(database);

  if(decoy){
  fprintf(output, "H\tComment\tDatabase shuffled; these are decoy matches\n");
  }
  fprintf(output, "H\tDBSeqLength\t?\n");
  fprintf(output, "H\tDBLocusCount\t%d\n", num_proteins);

  MASS_TYPE_T mass_type = get_mass_type_parameter("isotopic-mass");
  char temp_str[64];
  mass_type_to_string(mass_type, temp_str);
  fprintf(output, "H\tPrecursorMasses\t%s\n", temp_str);
  
  mass_type = get_mass_type_parameter("fragment-mass");
  mass_type_to_string(mass_type, temp_str);
  fprintf(output, "H\tFragmentMasses\t%s\n", temp_str); //?????????

  double tol = get_double_parameter("mass-window");
  fprintf(output, "H\tAlg-PreMasTol\t%.1f\n",tol);
  fprintf(output, "H\tAlg-FragMassTol\t%.2f\n", 
          get_double_parameter("ion-tolerance"));
  fprintf(output, "H\tAlg-XCorrMode\t0\n");

  SCORER_TYPE_T score = get_scorer_type_parameter("prelim-score-type");
  scorer_type_to_string(score, temp_str);
  fprintf(output, "H\tComment\tpreliminary algorithm %s\n", temp_str);

  score = get_scorer_type_parameter("score-type");
  scorer_type_to_string(score, temp_str);
  fprintf(output, "H\tComment\tfinal algorithm %s\n", temp_str);

  /*
  int alphabet_size = get_alphabet_size(PROTEIN_ALPH);
  char* alphabet = get_alphabet(FALSE);
  */

  int aa = 0;
  char aa_str[2];
  aa_str[1] = '\0';
  int alphabet_size = (int)'A' + ((int)'Z'-(int)'A');
  MASS_TYPE_T isotopic_type = get_mass_type_parameter("isotopic-mass");

  for(aa = (int)'A'; aa < alphabet_size -1; aa++){
    aa_str[0] = (char)aa;
    double mod = get_double_parameter(aa_str);
    if( mod != 0 ){
      //      double mass = mod + get_mass_amino_acid(aa, isotopic_type);
      double mass = get_mass_amino_acid(aa, isotopic_type);
      fprintf(output, "H\tStaticMod\t%s=%.3f\n", aa_str, mass);
    }
  }
  //for letters in alphabet
  //  double mod = get_double_parameter(letter);
  //  if mod != 0
  //     double mass = mod + getmass(letter);
  //     fprintf(output, "H\tStaticMod\t%s=%.3f\n", letter, mass);
  //  fprintf(output, "H\tStaticMod\tC=160.139\n");
  fprintf(output, "H\tAlg-DisplayTop\t%d\n", 
          get_int_parameter("max-sqt-result")); 
  // this is not correct for an sqt from analzyed matches

  PEPTIDE_TYPE_T cleavages = get_peptide_type_parameter("cleavages");
  peptide_type_to_string(cleavages, temp_str);
  fprintf(output, "H\tEnzymeSpec\t%s\n", temp_str);

  // write a comment that says what the scores are
  fprintf(output, "H\tLine fields: S, scan number, scan number,"
          "charge, 0, precursor m/z, 0, 0, number of matches\n");

  // fancy logic for printing the scores. see match.c:print_match_sqt

  SCORER_TYPE_T main_score = get_scorer_type_parameter("score-type");
  SCORER_TYPE_T other_score = get_scorer_type_parameter("prelim-score-type");
  ALGORITHM_TYPE_T analysis_score = get_algorithm_type_parameter("algorithm");
  if( is_analysis == TRUE && analysis_score == PERCOLATOR_ALGORITHM){
    main_score = PERCOLATOR_SCORE; 
    other_score = Q_VALUE;
  }else if( is_analysis == TRUE && analysis_score == QVALUE_ALGORITHM ){
    main_score = LOGP_QVALUE_WEIBULL_XCORR;
  }

  char main_score_str[64];
  scorer_type_to_string(main_score, main_score_str);
  char other_score_str[64];
  scorer_type_to_string(other_score, other_score_str);

  // ranks are always xcorr and sp
  // main/other scores from search are...xcorr/sp (OK as is)
  // ...p-val/xcorr
  if( main_score == LOGP_BONF_WEIBULL_XCORR ){
    strcpy(main_score_str, "log(p-value)");
    strcpy(other_score_str, "xcorr");
  }// main/other scores from analyze are perc/q-val (OK as is)
   // q-val/xcorr
  if( main_score == LOGP_QVALUE_WEIBULL_XCORR ){
    strcpy(main_score_str, "q-value");  // to be changed to curve-fit-q-value
    strcpy(other_score_str, "xcorr");
  }

  fprintf(output, "H\tLine fields: M, rank by xcorr score, rank by sp score, "
          "peptide mass, deltaCn, %s score, %s score, number ions matched, "
          "total ions compared, sequence\n", main_score_str, other_score_str);
}

/**
 * \brief Print the psm features to file in sqt format.
 *
 * Prints one S line, 'top_match' M lines, and one locus line for each
 * peptide source of each M line.
 * Assumes one spectrum per match collection.  Could get top_match,
 * score types from parameter.c.  Could get spectrum from first match.
 *\returns TRUE, if sucessfully print sqt format of the PSMs, else FALSE 
 */
BOOLEAN_T print_match_collection_sqt(
  FILE* output,                  ///< the output file -out
  int top_match,                 ///< the top matches to output -in
  MATCH_COLLECTION_T* match_collection,
  ///< the match_collection to print sqt -in
  SPECTRUM_T* spectrum,          ///< the spectrum to print sqt -in
  SCORER_TYPE_T prelim_score,    ///< the preliminary score to report -in
  SCORER_TYPE_T main_score       ///< the main score to report -in
  )
{

  if( output == NULL ){
    return FALSE;
  }
  time_t hold_time;
  hold_time = time(0);
  int charge = match_collection->charge; 
  int num_matches = match_collection->experiment_size;

  // First, print spectrum info
  print_spectrum_sqt(spectrum, output, num_matches, charge);
  
  MATCH_T* match = NULL;
  
  // create match iterator
  // TRUE: return match in sorted order of main_score type
  MATCH_ITERATOR_T* match_iterator = 
    new_match_iterator(match_collection, main_score, TRUE);
  
  // Second, iterate over matches, prints M and L lines
  int match_count = 0;
  while(match_iterator_has_next(match_iterator)){
    ++match_count;
    match = match_iterator_next(match_iterator);    

    print_match_sqt(match, output, main_score, prelim_score);

    // print only up to max_rank_result of the matches
    // FIXME (BF 29-10-08): print those with rank 1 to top_match
    if(match_count >= top_match){
      break;
    }
  }// next match
  
  free_match_iterator(match_iterator);
  
  return TRUE;
}

/**
 * match_iterator routines!
 *
 */

/**
 * create a new memory allocated match iterator, which iterates over
 * match iterator only one iterator is allowed to be instantiated per
 * match collection at a time 
 *\returns a new memory allocated match iterator
 */
MATCH_ITERATOR_T* new_match_iterator(
  MATCH_COLLECTION_T* match_collection,
  ///< the match collection to iterate -out
  SCORER_TYPE_T score_type,
  ///< the score type to iterate (LOGP_EXP_SP, XCORR) -in
  BOOLEAN_T sort_match  ///< should I return the match in sorted order?
  )
{
  // TODO (BF 06-Feb-08): Could we pass back an iterator with has_next==False
  if (match_collection == NULL){
    die("Null match collection passed to match iterator");
  }
  // is there any existing iterators?
  if(match_collection->iterator_lock){
    carp(CARP_FATAL, 
         "Can only have one match iterator instantiated at a time");
    exit(1);
  }
  
  // has the score type been populated in match collection?
  if(!match_collection->scored_type[score_type]){
    carp(CARP_FATAL, 
         "The match collection has not been scored for request score type.");
    exit(1);
  }
  
  // allocate a new match iterator
  MATCH_ITERATOR_T* match_iterator = 
    (MATCH_ITERATOR_T*)mycalloc(1, sizeof(MATCH_ITERATOR_T));
  
  // set items
  match_iterator->match_collection = match_collection;
  match_iterator->match_mode = score_type;
  match_iterator->match_idx = 0;
  match_iterator->match_total = match_collection->match_total;

  // only sort if requested and match collection is not already sorted
  if(sort_match && (match_collection->last_sorted != score_type 
  /*|| (match_collection->last_sorted == SP && score_type == LOGP_EXP_SP)*/)){

    if((score_type == LOGP_EXP_SP || score_type == LOGP_BONF_EXP_SP ||
        score_type == LOGP_WEIBULL_SP || score_type == LOGP_BONF_WEIBULL_SP)
       &&
       match_collection->last_sorted == SP){
      // No need to sort, since the score_type has same rank as SP      
    }
    
    else if((score_type == LOGP_EVD_XCORR || score_type ==LOGP_BONF_EVD_XCORR)
            && match_collection->last_sorted == XCORR){
      // No need to sort, since the score_type has same rank as XCORR
    }
    else if((score_type == Q_VALUE) &&
            match_collection->last_sorted == PERCOLATOR_SCORE){
      // No need to sort, the score_type has same rank as PERCOLATOR_SCORE
    }
    // sort match collection by score type
    else if(!sort_match_collection(match_collection, score_type)){
      carp(CARP_FATAL, "failed to sort match collection");
      free_match_collection(match_collection);
      free(match_iterator);
      exit(1);
    }
  }

  // ok lock up match collection
  match_collection->iterator_lock = TRUE;
  
  return match_iterator;
}

/**
 * \brief Create a match iterator to return matches from a collection
 * grouped by spectrum and sorted by given score type.
 *
 * \returns A heap-allocated match iterator.
 */
MATCH_ITERATOR_T* new_match_iterator_spectrum_sorted(
  MATCH_COLLECTION_T* match_collection,  ///< for iteration -in
  SCORER_TYPE_T scorer ///< the score type to sort by -in
){

  MATCH_ITERATOR_T* match_iterator = 
    (MATCH_ITERATOR_T*)mycalloc(1, sizeof(MATCH_ITERATOR_T));

  // set up fields
  match_iterator->match_collection = match_collection;
  match_iterator->match_mode = scorer;
  match_iterator->match_idx = 0;
  match_iterator->match_total = match_collection->match_total;

  spectrum_sort_match_collection(match_collection, scorer);

  match_collection->iterator_lock = TRUE;

  return match_iterator;
}

/**
 * Does the match_iterator have another match struct to return?
 *\returns TRUE, if match iter has a next match, else False
 */
BOOLEAN_T match_iterator_has_next(
  MATCH_ITERATOR_T* match_iterator ///< the working  match iterator -in
  )
{
  return (match_iterator->match_idx < match_iterator->match_total);
}

/**
 * return the next match struct!
 *\returns the match in decreasing score order for the match_mode(SCORER_TYPE_T)
 */
MATCH_T* match_iterator_next(
  MATCH_ITERATOR_T* match_iterator ///< the working match iterator -in
  )
{
  return match_iterator->match_collection->match[match_iterator->match_idx++];
}

/**
 * free the memory allocated iterator
 */
void free_match_iterator(
  MATCH_ITERATOR_T* match_iterator ///< the match iterator to free
  )
{
  // free iterator
  if (match_iterator != NULL){
    if (match_iterator->match_collection != NULL){
      match_iterator->match_collection->iterator_lock = FALSE;
    }
    free(match_iterator);
  }
}

/*
 * Copied from spectrum_collection::serialize_header
 * uses values from paramter.c rather than taking as arguments
 */
void serialize_headers(FILE** psm_file_array){

  if( *psm_file_array == NULL ){
    return;
  }
  int num_spectrum_features = 0; //obsolete?
  int num_charged_spectra = 0;  //this is set later
  int matches_per_spectrum = get_int_parameter("top-match");
  char* filename = get_string_parameter_pointer("protein input");
  char* protein_file = parse_filename(filename);
  //  free(filename);
  filename = get_string_parameter_pointer("ms2 file");
  char* ms2_file = parse_filename(filename);
  //  free(filename);
           

  //write values to files
  int total_files = 1 + get_int_parameter("number-decoy-set");
  int i=0;
  for(i=0; i<total_files; i++){
    fwrite(&(num_charged_spectra), sizeof(int), 1, psm_file_array[i]);
    fwrite(&(num_spectrum_features), sizeof(int), 1, psm_file_array[i]);
    fwrite(&(matches_per_spectrum), sizeof(int), 1, psm_file_array[i]);
  }
  
  free(protein_file);
  free(ms2_file);

}

/**
 * \brief Writes the contents of a match_collection to file(s)
 *
 * \details Takes information from parameter.c to decide which files
 * (binary, sqt) to write to, how many matches to write, etc.
 *
 */
void print_matches( 
                   MATCH_COLLECTION_T* match_collection, ///< results to write
                   SPECTRUM_T* spectrum, ///< results for this spectrum
                   BOOLEAN_T is_decoy,   ///< peptides from target/decoy
                   FILE* psm_file,       ///< binary file -out
                   FILE* sqt_file,       ///< text file, target -out
                   FILE* decoy_file){    ///< text file, decoy -out

  carp(CARP_DETAILED_DEBUG, "Writing matches to file");
  // get parameters
  MATCH_SEARCH_OUTPUT_MODE_T output_type = get_output_type_parameter(
                                                            "output-mode");
  int max_sqt_matches = get_int_parameter("max-sqt-result");
  int max_psm_matches = get_int_parameter("top-match");
  SCORER_TYPE_T main_score = get_scorer_type_parameter("score-type");
  SCORER_TYPE_T prelim_score = get_scorer_type_parameter("prelim-score-type");


  // write binary files
  if( output_type != SQT_OUTPUT ){ //i.e. binary or all
    carp(CARP_DETAILED_DEBUG, "Serializing psms");
    serialize_psm_features(match_collection, psm_file, max_psm_matches,
                           prelim_score, main_score);
  }

  // write sqt files
  if( output_type != BINARY_OUTPUT ){ //i.e. sqt or all
    carp(CARP_DETAILED_DEBUG, "Writing sqt results");
    if( ! is_decoy ){
      print_match_collection_sqt(sqt_file, max_sqt_matches,
                                 match_collection, spectrum,
                                 prelim_score, main_score);
    }else{
      print_match_collection_sqt(decoy_file, max_sqt_matches,
                                 match_collection, spectrum,
                                 prelim_score, main_score);
    }
  }
}

/*******************************************
 * match_collection post_process extension
 ******************************************/

/**
 * \brief Creates a new match_collection from the PSM iterator.
 *
 * Used in the post_processing extension.  Also used by
 * setup_match_collection_iterator which is called by next to find,
 * open, and parse the next psm file(s) to process.  If there are
 * multiple target psm files, it reads in all of them when set_type is
 * 0 and puts them all into one match_collection. 
 *\returns A heap allocated match_collection.
 */
MATCH_COLLECTION_T* new_match_collection_psm_output(
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator, 
    ///< the working match_collection_iterator -in
  SET_TYPE_T set_type  
    ///< what set of match collection are we creating? (TARGET, DECOY1~3) -in 
  )
{ 
  struct dirent* directory_entry = NULL;
  char* file_in_dir = NULL;
  FILE* result_file = NULL;
  char suffix[25];
  //  char prefix[25];

  carp(CARP_DEBUG, "Calling new_match_collection_psm_output");
  DATABASE_T* database = match_collection_iterator->database;
  
  // allocate match_collection object
  MATCH_COLLECTION_T* match_collection = allocate_match_collection();

  // set this as a post_process match collection
  match_collection->post_process_collection = TRUE;
  
  // the protein counter size, create protein counter
  match_collection->post_protein_counter_size 
   = get_database_num_proteins(database);
  match_collection->post_protein_counter 
   = (int*)mycalloc(match_collection->post_protein_counter_size, sizeof(int));
  match_collection->post_protein_peptide_counter 
   = (int*)mycalloc(match_collection->post_protein_counter_size, sizeof(int));

  // create hash table for peptides
  // Set initial capacity to protein count.
  match_collection->post_hash 
    = new_hash(match_collection->post_protein_counter_size);
  
  // set the prefix of the serialized file to parse
  // Also, tag if match_collection type is null_peptide_collection

  /*char* filepath = match_collection_iterator->directory_name;
  char** file_path_array = parse_filename_path_extension(filepath, ".csm");
  char* file_prefix = file_path_array[0];
  char* directory_name = file_path_array[1];
  if( directory_name == NULL ){
    directory_name = ".";
    }*/

  if(set_type == SET_TARGET){
    //    sprintf(suffix, "crux_match_target");
    //sprintf(prefix, "crux_match_target");
    sprintf(suffix, ".csm");
    match_collection->null_peptide_collection = FALSE;
  }
  else{
    //    sprintf(suffix, "crux_match_decoy_%d", (int)set_type);
    //sprintf(prefix, "crux_match_decoy_%d", (int)set_type);
    sprintf(suffix, "-decoy-%d.csm", (int)set_type);
    match_collection->null_peptide_collection = TRUE;
  }
  
  carp(CARP_DEBUG, "Set type is %d and suffix is %s", (int)set_type, suffix);
  // iterate over all PSM files in directory to find the one to read
  while((directory_entry 
            = readdir(match_collection_iterator->working_directory))){

    //carp(CARP_DETAILED_DEBUG, "Next file is %s", directory_entry->d_name);

    if (strcmp(directory_entry->d_name, ".") == 0 ||
        strcmp(directory_entry->d_name, "..") == 0 ||
        !suffix_compare(directory_entry->d_name, suffix)
        //!prefix_compare(directory_entry->d_name, prefix)
        ) {
      continue;
    }

    if( set_type == SET_TARGET && name_is_decoy(directory_entry->d_name) ){
      continue;
    }
    file_in_dir =get_full_filename(match_collection_iterator->directory_name, 
                                   directory_entry->d_name);

    carp(CARP_INFO, "Getting PSMs from %s", file_in_dir);
    result_file = fopen(file_in_dir, "r");
    if( access(file_in_dir, R_OK)){
      carp(CARP_FATAL, "Cannot read from psm file '%s'", file_in_dir);
      exit(1);
    }
    // add all the match objects from result_file
    extend_match_collection(match_collection, database, result_file);
    carp(CARP_DETAILED_DEBUG, "Extended match collection " );
    fclose(result_file);
    free(file_in_dir);
    carp(CARP_DETAILED_DEBUG, "Finished file.");
  }
  
  return match_collection;
}


/**
 * parse all the match objects and add to match collection
 *\returns TRUE, if successfully parse all PSMs in result_file, else FALSE
 */
BOOLEAN_T extend_match_collection(
  MATCH_COLLECTION_T* match_collection, ///< match collection to extend -out
  DATABASE_T* database, ///< the database holding the peptides -in
  FILE* result_file   ///< the result file to parse PSMs -in
  )
{
  int total_spectra = 0;
  int match_idx = 0;
  int spectrum_idx = 0;
  int charge = 0;
  MATCH_T* match = NULL;
  int num_top_match = 0;
  int num_spectrum_features = 0;
  float delta_cn =  0;
  float ln_delta_cn = 0;
  float ln_experiment_size = 0;
  int match_total_of_serialized_collection = 0;
  int score_type_idx = 0;
  BOOLEAN_T type_scored = FALSE;

  // only for post_process_collections
  if(!match_collection->post_process_collection){
    carp(CARP_ERROR, "Must be a post process match collection");
    return FALSE;
  }
  
  // read in file specific info
  
  // get number of spectra serialized in the file
  if(fread(&total_spectra, (sizeof(int)), 1, result_file) != 1){
    carp(CARP_ERROR,"Serialized file corrupted, incorrect number of spectra");
    return FALSE;
  }
  carp(CARP_DETAILED_DEBUG, "There are %i spectra in the result file", 
       total_spectra);

  // FIXME unused feature, just set to 0
  // get number of spectra features serialized in the file
  if(fread(&num_spectrum_features, (sizeof(int)), 1, result_file) != 1){
    carp(CARP_ERROR, 
         "Serialized file corrupted, incorrect number of spectrum features");
    return FALSE;
  }
  
  carp(CARP_DETAILED_DEBUG, "There are %i spectrum features", 
       num_spectrum_features);

  // get number top ranked peptides serialized
  if(fread(&num_top_match, (sizeof(int)), 1, result_file) != 1){
    carp(CARP_ERROR, 
         "Serialized file corrupted, incorrect number of top match");  
    return FALSE;
  }
  carp(CARP_DETAILED_DEBUG, "There are %i top matches", num_top_match);

  // FIXME
  // could parse fasta file and ms2 file
  
  
  // now iterate over all spectra serialized
  for(spectrum_idx = 0; spectrum_idx < total_spectra; ++spectrum_idx){
    /*** get all spectrum specific features ****/
    
    // get charge of the spectrum
    /*    if(fread(&charge, (sizeof(int)), 1, result_file) != 1){
      carp(CARP_ERROR, "Serialized file corrupted, charge value not read");  
      return FALSE;
    }
    */
    int chars_read = fread(&charge, (sizeof(int)), 1, result_file);
    carp(CARP_DETAILED_DEBUG, "Read %i characters, charge is %i",
         chars_read, charge);

    // get serialized match_total
    /*if(fread(&match_total_of_serialized_collection, (sizeof(int)),
             1, result_file) != 1){
      carp(CARP_ERROR, "Serialized file corrupted, "
          "incorrect match_total_of_serialized_collection value");  
      return FALSE;
      }*/
    chars_read = fread(&match_total_of_serialized_collection, (sizeof(int)),
                       1, result_file);
    carp(CARP_DETAILED_DEBUG, "Read %i characters, match total is %i",
         chars_read, match_total_of_serialized_collection);
      
    // get delta_cn value
    if(fread(&delta_cn, (sizeof(float)), 1, result_file) != 1){
      carp(CARP_ERROR, 
       "Serialized file corrupted, incorrect delta cn value for top match");  
      return FALSE;
    }
    
    // get ln_delta_cn value
    if(fread(&ln_delta_cn, (sizeof(float)), 1, result_file) != 1){
      carp(CARP_ERROR, 
    "Serialized file corrupted, incorrect ln_delta cn value for top match");  
      return FALSE;
    }
    
    // get ln_experiment_size
    if(fread(&ln_experiment_size, (sizeof(float)), 1, result_file) != 1){
      carp(CARP_ERROR, "Serialized file corrupted, incorrect "
           "ln_experiment_size cn value for top match");  
      return FALSE;
    }
    
    // Read each boolean for scored type 
    // parse all boolean indicators for scored match object
    for(score_type_idx=0; score_type_idx < _SCORE_TYPE_NUM; ++score_type_idx){
      fread(&(type_scored), sizeof(BOOLEAN_T), 1, result_file);
      
      // if this is the first time extending the match collection
      // set scored boolean values
      if(!match_collection->post_scored_type_set){
        match_collection->scored_type[score_type_idx] = type_scored;
      }
      else{
        // if boolean values already set compare if no
        // conflicting scored types 
        if(match_collection->scored_type[score_type_idx] != type_scored){
          carp(CARP_ERROR, "Serialized match objects has not been scored "
               "as other match objects");
        }
      }
      
      // now once we are done with setting scored type
      // set match collection status as set!
      if(!match_collection->post_scored_type_set &&
         score_type_idx == (_SCORE_TYPE_NUM-1)){
        match_collection->post_scored_type_set = TRUE;
      }
    }
    
    // now iterate over all 
    for(match_idx = 0; match_idx < num_top_match; ++match_idx){
      // break if there are no match objects serialized
      //      if(match_total_of_serialized_collection <= 0){
      if(match_total_of_serialized_collection <= match_idx){
        break;
      }
      
      carp(CARP_DETAILED_DEBUG, "Reading match %i", match_idx);
      // parse match object
      if((match = parse_match(result_file, database))==NULL){
        carp(CARP_ERROR, "Failed to parse serialized PSM match");
        return FALSE;
      }
      
      // set all spectrum specific features to parsed match
      set_match_charge(match, charge);
      set_match_delta_cn(match, delta_cn);
      set_match_ln_delta_cn(match, ln_delta_cn);
      set_match_ln_experiment_size(match, ln_experiment_size);
      
      // now add match to match collection
      add_match_to_match_collection(match_collection, match);
    }// next match for this spectrum

  }// next spectrum
  
  return TRUE;
}

/**
 * Adds the match object to match_collection
 * Must not exceed the _MAX_NUMBER_PEPTIDES to be match added
 * \returns TRUE if successfully adds the match to the
 * match_collection, else FALSE 
 */
BOOLEAN_T add_match_to_match_collection(
  MATCH_COLLECTION_T* match_collection, ///< the match collection to free -out
  MATCH_T* match ///< the match to add -in
  )
{
  PEPTIDE_T* peptide = NULL;

  // only for post_process_collections
  if(!match_collection->post_process_collection){
    carp(CARP_ERROR, "Must be a post process match collection");
    return FALSE;
  }
  
  // check if enough space for peptide match
  if(match_collection->match_total >= _MAX_NUMBER_PEPTIDES){
    carp(CARP_ERROR, "Rich match count exceeds max match limit: %d", 
         _MAX_NUMBER_PEPTIDES);
    return FALSE;
  }
  
  // add a new match to array
  match_collection->match[match_collection->match_total] = match;
  
  // increment total rich match count
  ++match_collection->match_total;
  
  // DEBUG, print total peptided scored so far
  if(match_collection->match_total % 1000 == 0){
    carp(CARP_INFO, "parsed PSM: %d", match_collection->match_total);
  }
  
  // match peptide
  peptide = get_match_peptide(match);
  
  // update protein counter, protein_peptide counter
  update_protein_counters(match_collection, peptide);
  
  // update hash table
  char* hash_value = get_peptide_hash_value(peptide); 
  add_hash(match_collection->post_hash, hash_value, NULL); 
  
  return TRUE;
}

/**
 * updates the protein_counter and protein_peptide_counter for 
 * run specific features
 */
void update_protein_counters(
  MATCH_COLLECTION_T* match_collection, ///< working match collection -in
  PEPTIDE_T* peptide  ///< peptide information to update counters -in
  )
{
  PEPTIDE_SRC_ITERATOR_T* src_iterator = NULL;
  PEPTIDE_SRC_T* peptide_src = NULL;
  PROTEIN_T* protein = NULL;
  unsigned int protein_idx = 0;
  int hash_count = 0;
  BOOLEAN_T unique = FALSE;
  
  // only for post_process_collections
  if(!match_collection->post_process_collection){
    carp(CARP_ERROR, "Must be a post process match collection");
    exit(1);
  }
  
  // See if this peptide has been observed before?
  char* hash_value = get_peptide_hash_value(peptide);
  hash_count = get_hash_count(match_collection->post_hash, hash_value);
  free(hash_value);

  if(hash_count < 1){
    // yes this peptide is first time observed
    unique = TRUE;
  }

  // first update protein counter
  src_iterator = new_peptide_src_iterator(peptide);
  
  // iterate overall parent proteins
  while(peptide_src_iterator_has_next(src_iterator)){
    peptide_src = peptide_src_iterator_next(src_iterator);
    protein = get_peptide_src_parent_protein(peptide_src);
    protein_idx = get_protein_protein_idx(protein);
    
    // update the number of PSM this protein matches
    ++match_collection->post_protein_counter[protein_idx];
    
    // number of peptides match this protein
    if(unique){
      ++match_collection->post_protein_peptide_counter[protein_idx];
    }
  }  
  
  free_peptide_src_iterator(src_iterator);
}

/**
 * Fill the match objects score with the given the float array. 
 * The match object order must not have been altered since scoring.
 * The result array size must match the match_total count.
 * Match ranks are also populated to preserve the original order of the
 * match input TRUE for preserve_order.
 *\returns TRUE, if successfully fills the scores into match object, else FALSE.
 */
BOOLEAN_T fill_result_to_match_collection(
  MATCH_COLLECTION_T* match_collection, 
    ///< the match collection to iterate -out
  double* results,  
    ///< The result score array to fill the match objects -in
  SCORER_TYPE_T score_type,  
    ///< The score type of the results to fill (XCORR, Q_VALUE, ...) -in
  BOOLEAN_T preserve_order 
    ///< preserve match order?
  )
{
  int match_idx = 0;
  MATCH_T* match = NULL;
  MATCH_T** match_array = NULL;
  SCORER_TYPE_T score_type_old = match_collection->last_sorted;

  // iterate over match object in collection, set scores
  for(; match_idx < match_collection->match_total; ++match_idx){
    match = match_collection->match[match_idx];
    set_match_score(match, score_type, results[match_idx]);    
  }
  
  // if need to preserve order store a copy of array in original order 
  if(preserve_order){
    match_array = (MATCH_T**)mycalloc(match_collection->match_total, sizeof(MATCH_T*));
    for(match_idx=0; match_idx < match_collection->match_total; ++match_idx){
      match_array[match_idx] = match_collection->match[match_idx];
    }
  }

  // populate the rank of match_collection
  if(!populate_match_rank_match_collection(match_collection, score_type)){
    carp(CARP_ERROR, "failed to populate match rank in match_collection");
    free_match_collection(match_collection);
    exit(1);
  }
  
  // restore match order if needed
  if(preserve_order){
    for(match_idx=0; match_idx < match_collection->match_total; ++match_idx){
      match_collection->match[match_idx] = match_array[match_idx];
    }
    match_collection->last_sorted = score_type_old;
    free(match_array);
  }

  match_collection->scored_type[score_type] = TRUE;
  
  return TRUE;
}

/**
 * Process run specific features from all the PSMs
 */
void process_run_specific_features(
  MATCH_COLLECTION_T* match_collection ///< the match collection to free -out
  );


/**********************************
 * match_collection get, set methods
 **********************************/

/**
 *\returns the match_collection protein counter for the protein idx
 */
int get_match_collection_protein_counter(
  MATCH_COLLECTION_T* match_collection, ///< the working match collection -in
  unsigned int protein_idx ///< the protein index to return protein counter -in
  )
{
  // only for post_process_collections
  if(!match_collection->post_process_collection){
    carp(CARP_ERROR, "Must be a post process match collection");
    exit(1);
  }

  // number of PSMs match this protein
  return match_collection->post_protein_counter[protein_idx];
}

/**
 *\returns the match_collection protein peptide counter for the protein idx
 */
int get_match_collection_protein_peptide_counter(
  MATCH_COLLECTION_T* match_collection, ///< the working match collection -in
  unsigned int protein_idx ///< the protein index to return protein peptiide counter -in
  )
{
  // only for post_process_collections
  if(!match_collection->post_process_collection){
    carp(CARP_ERROR, "Must be a post process match collection");
    exit(1);
  }
  
  // number of peptides match this protein
  return match_collection->post_protein_peptide_counter[protein_idx];
}

/**
 *\returns the match_collection hash value of PSMS for which this is the best scoring peptide
 */
int get_match_collection_hash(
  MATCH_COLLECTION_T* match_collection, ///< the working match collection -in
  PEPTIDE_T* peptide  ///< the peptide to check hash value -in
  )
{
  // only for post_process_collections
  if(!match_collection->post_process_collection){
    carp(CARP_ERROR, "Must be a post process match collection");
    exit(1);
  }
  
  char* hash_value = get_peptide_hash_value(peptide);
  int count = get_hash_count(match_collection->post_hash, hash_value);
  free(hash_value);
  
  return count;
}

/**
 * \brief Get the number of proteins in the database associated with
 * this match collection.
 */
int get_match_collection_num_proteins(
  MATCH_COLLECTION_T* match_collection ///< the match collection of interest -
  ){

  return match_collection->post_protein_counter_size;
}


/******************************
 * match_collection_iterator
 ******************************/
     
/**
 * \brief Finds the next match_collection in directory and prepares
 * the iterator to hand it off when 'next' called.
 *
 * When no more match_collections (i.e. psm files) are available, set
 * match_collection_iterator->is_another_collection to FALSE
 * \returns void
 */
void setup_match_collection_iterator(
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator 
    ///< the match_collection_iterator to set up -in/out
  )
{
  // are there any more match_collections to return?
  if(match_collection_iterator->collection_idx 
      < match_collection_iterator->number_collections){

    // then go parse the match_collection
    match_collection_iterator->match_collection = 
      new_match_collection_psm_output(match_collection_iterator, 
         (SET_TYPE_T)match_collection_iterator->collection_idx);

    // we have another match_collection to return
    match_collection_iterator->is_another_collection = TRUE;
    
    // let's move on to the next one next time
    ++match_collection_iterator->collection_idx;

    // reset directory
    rewinddir(match_collection_iterator->working_directory);
  }
  else{
    // we're done, no more match_collections to return
    match_collection_iterator->is_another_collection = FALSE;
  }
}

/**
 * Create a match_collection iterator from a directory of serialized files
 * Only hadles up to one target and three decoy sets per folder
 *\returns match_collection iterator instantiated from a result folder
 */
MATCH_COLLECTION_ITERATOR_T* new_match_collection_iterator(
  char* output_file_directory, 
    ///< the directory path where the PSM output files are located -in
  char* fasta_file 
    ///< The name of the fasta file for peptides for match_collections. -in
  )
{
  carp(CARP_DEBUG, 
       "Creating match collection iterator for dir %s and protein input %s",
       output_file_directory, fasta_file);

  // allocate match_collection
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator =
    (MATCH_COLLECTION_ITERATOR_T*)
      mycalloc(1, sizeof(MATCH_COLLECTION_ITERATOR_T));

  DIR* working_directory = NULL;
  struct dirent* directory_entry = NULL;
  DATABASE_T* database = NULL;
  BOOLEAN_T use_index_boolean = get_boolean_parameter("use-index");  

  // get directory from path name and prefix from filename
  /*
  char** file_path_array = parse_filename_path_extension(  // free me
                                     output_file_directory, ".csm");
  char* filename = parse_filename(output_file_directory);  // free me
  char* filename_prefix = file_path_array[0];
  char* decoy_prefix = cat_string(filename_prefix, "-decoy-");// free me
  char* psm_dir_name = file_path_array[1];
  if( psm_dir_name == NULL ){
    psm_dir_name = ".";
  }
  */

  /*
    BF: I think that this step is to count how many decoys there are
    per target file.  This is prone to errors as all it really does is
    check for the presence of a file with *decoy_1*, and one with
    *decoy_2* and *decoy_3*.  In fact, the three files could be from
    different targets.  Nothing was being done with the check for a
    target file.  There must be a better way to do this.
   */


  // do we have these files in the directory
  BOOLEAN_T boolean_result = FALSE;
  BOOLEAN_T decoy_1 = FALSE;
  BOOLEAN_T decoy_2 = FALSE;
  BOOLEAN_T decoy_3 = FALSE;

  // open PSM file directory
  working_directory = opendir(output_file_directory);
  //working_directory = opendir(psm_dir_name);
  
  if(working_directory == NULL){
    carp(CARP_FATAL, "Failed to open PSM file directory: %s", 
        output_file_directory);
    exit(1);
  }
  
  // determine how many decoy sets we have
  while((directory_entry = readdir(working_directory))){
    //if(prefix_compare(directory_entry->d_name, "crux_match_target")){
    /*if(suffix_compare(directory_entry->d_name, ".csm")){
      carp(CARP_DEBUG, "Found target file %s", directory_entry->d_name);
      boolean_result = TRUE;
    }
    //else if(prefix_compare(directory_entry->d_name, "crux_match_decoy_1")) {
    else*/ if(suffix_compare(directory_entry->d_name, "decoy-1.csm")) {
      carp(CARP_DEBUG, "Found decoy file %s", directory_entry->d_name);
      decoy_1 = TRUE;
      //num_decoys++;
    }
    //else if(prefix_compare(directory_entry->d_name, "crux_match_decoy_2")) {
    else if(suffix_compare(directory_entry->d_name, "decoy-2.csm")) {
      decoy_2 = TRUE;
    }
    //else if(prefix_compare(directory_entry->d_name, "crux_match_decoy_3")) {
    else if(suffix_compare(directory_entry->d_name, "decoy-3.csm")) {
      decoy_3 = TRUE;
      break;
    }    
    else if(suffix_compare(directory_entry->d_name, ".csm")){
      carp(CARP_DEBUG, "Found target file %s", directory_entry->d_name);
      boolean_result = TRUE;
    }
  }
  
  // set total_sets count
  int total_sets = 0;

  if(decoy_3){
    total_sets = 4; // 3 decoys + 1 target
  }
  else if(decoy_2){
    total_sets = 3; // 2 decoys + 1 target
  }
  else if(decoy_1){
    total_sets = 2; // 1 decoys + 1 target
  }
  else{
    total_sets = 1;
    carp(CARP_INFO, "No decoy sets exist in directory: %s", 
        output_file_directory);
  }
  if(!boolean_result){
    carp(CARP_FATAL, "No PSM files found in directory '%s'", 
         output_file_directory);
    exit(1);
  }

  // get binary fasta file name with path to crux directory 
  //  char* binary_fasta = get_binary_fasta_name_in_crux_dir(fasta_file);
  char* binary_fasta  = NULL;
  if (use_index_boolean == TRUE){ 
    binary_fasta = get_index_binary_fasta_name(fasta_file);
  } else {
    binary_fasta = get_binary_fasta_name(fasta_file);
    carp(CARP_DEBUG, "Looking for binary fasta %s", binary_fasta);
    if (access(binary_fasta, F_OK)){
      carp(CARP_DEBUG, "Could not find binary fasta %s", binary_fasta);
      if (!create_binary_fasta_here(fasta_file, binary_fasta)){
       die("Could not create binary fasta file %s", binary_fasta);
      };
    }
  }
  
  // check if input file exist
  if(access(binary_fasta, F_OK)){
    carp(CARP_FATAL, "The file \"%s\" does not exist (or is not readable, "
        "or is empty) for crux index.", binary_fasta);
    free(binary_fasta);
    exit(1);
  }
  
  carp(CARP_DEBUG, "Creating a new database");
  // now create a database, 
  // using fasta file either binary_file(index) or fastafile
  database = new_database(binary_fasta, TRUE);
  
  // check if already parsed
  if(!get_database_is_parsed(database)){
    carp(CARP_DETAILED_DEBUG,"Parsing database");
    if(!parse_database(database)){
      carp(CARP_FATAL, "Failed to parse database, cannot create new index");
      free_database(database);
      exit(1);
    }
  }
  
  free(binary_fasta);

  // reset directory
  rewinddir(working_directory);
  
  // set match_collection_iterator fields
  match_collection_iterator->working_directory = working_directory;
  match_collection_iterator->database = database;  
  match_collection_iterator->number_collections = total_sets;
  match_collection_iterator->directory_name = 
    my_copy_string(output_file_directory);
  match_collection_iterator->is_another_collection = FALSE;

  // setup the match collection iterator for iteration
  // here it will go parse files to construct match collections
  setup_match_collection_iterator(match_collection_iterator);

  // clean up strings
  //free(file_path_array);
  //free(filename);
  //free(decoy_prefix);

  return match_collection_iterator;
}

/**
 *\returns TRUE, if there's another match_collection to return, else return FALSE
 */
BOOLEAN_T match_collection_iterator_has_next(
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator ///< the working match_collection_iterator -in
  )
{
  // Do we have another match_collection to return
  return match_collection_iterator->is_another_collection;
}

/**
 * free match_collection_iterator
 */
void free_match_collection_iterator(
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator ///< the working match_collection_iterator -in
  )
{
  // free unclaimed match_collection
  if(match_collection_iterator->match_collection != NULL){
    free_match_collection(match_collection_iterator->match_collection);
  }
  
  // free up all match_collection_iterator 
  free(match_collection_iterator->directory_name);
  free_database(match_collection_iterator->database);
  closedir(match_collection_iterator->working_directory); 
  free(match_collection_iterator);
}

/**
 * \brief Fetches the next match collection object and prepares for
 * the next iteration 
 *\returns The next match collection object
 */
MATCH_COLLECTION_T* match_collection_iterator_next(
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator 
    ///< the working match_collection_iterator -in
  )
{
  MATCH_COLLECTION_T* match_collection = NULL;
  
  if(match_collection_iterator->is_another_collection){
    match_collection = match_collection_iterator->match_collection;
    match_collection_iterator->match_collection = NULL;
    setup_match_collection_iterator(match_collection_iterator);
    return match_collection;
  }
  else{
    carp(CARP_ERROR, "No match_collection to return");
    return NULL;
  }
}

/**
 *\returns the total number of match_collections to return
 */
int get_match_collection_iterator_number_collections(
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator ///< the working match_collection_iterator -in
  )
{
  return match_collection_iterator->number_collections;
}

/**
 * \brief Get the name of the directory the match_collection_iterator
 * is working in.
 * \returns A heap allocated string (char*) of the directory name.
 */
char* get_match_collection_iterator_directory_name(
  MATCH_COLLECTION_ITERATOR_T* iterator ///< the match_collection_iterator -in
  ){

  char* dir_name = my_copy_string(iterator->directory_name);

  return dir_name;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */

