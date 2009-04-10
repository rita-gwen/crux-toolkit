/*************************************************************************//**
 * \file match_analysis.c
 * AUTHOR: Chris Park
 * CREATE DATE: Jan 03 2007
 * \brief  Given as input a directory containing binary psm files,
 * a protein database, and an optional parameter file analyze the
 * matches (with percolator or q-value) and return scores indicating
 * how good the matches are. 
 *
 * Handles at most 4 files (target and decoy).  Expects psm files to
 * end with the extension '.csm' and decoys to end with
 * '-decoy#.csm'.  Multiple target files in the given directory are
 * concatinated together and presumed to be non-overlaping parts of
 * the same ms2 file. 
 * 
 * $Revision: 1.12 $
 ****************************************************************************/
#include "q-value.h"

#define MAX_PSMS 10000000
// 14th decimal place
#define EPSILON 0.00000000000001 
#define NUM_QVALUE_OPTIONS 7
#define NUM_QVALUE_ARGUMENTS 1

/* 
 * Private function declarations.  Details below
 */

MATCH_COLLECTION_T* run_qvalue(
  char* psm_result_folder, 
  char* fasta_file 
  ); 

static void print_text_files(
  MATCH_COLLECTION_T* match_collection,
  SCORER_TYPE_T scorer_type,
  SCORER_TYPE_T second_scorer_type
  );

  
/**
 * \brief One of the commands for crux.  Takes in a directory
 * containing binary psm files and a protein source (index or fasta
 * file) and calculates q-values based on the p-values calculated in
 * the search.
 */
int qvalue_main(int argc, char** argv){

  /* Define command line arguments */
  int num_options = NUM_QVALUE_OPTIONS;
  char* option_list[NUM_QVALUE_OPTIONS] = {
    "version",
    "verbosity",
    "parameter-file",
    "write-parameter-file",
    "overwrite",
    "output-dir",
    "fileroot"
  };

  int num_arguments = NUM_QVALUE_ARGUMENTS;
  char* argument_list[NUM_QVALUE_ARGUMENTS] = {
    "protein input"
  };

  /* for debugging handling of parameters*/
  set_verbosity_level(CARP_ERROR);

  /* Set up parameters and set defaults in parameter.c */
  initialize_parameters();

  /* Define optional and required arguments in parameter.c */
  select_cmd_line_options(option_list, num_options );
  select_cmd_line_arguments(argument_list, num_arguments);

  /* Parse the command line and optional paramter file
     does sytnax, type, and bounds checking and dies on error */
  parse_cmd_line_into_params_hash(argc, argv, "crux compute-q-values");

  /* Get arguments */
  char* psm_dir = get_string_parameter("output-dir");
  char* protein_input_name = get_string_parameter("protein input");

  /* Get options */
  MATCH_COLLECTION_T* match_collection = NULL;

  /* Perform the analysis */
  carp(CARP_INFO, "Running compute q-values");
  match_collection = run_qvalue(psm_dir, protein_input_name);
  SCORER_TYPE_T scorer_type =  LOGP_QVALUE_WEIBULL_XCORR; 
  SCORER_TYPE_T second_scorer_type = XCORR; // could it be other?

  carp(CARP_INFO, "Outputting matches.");
  print_text_files(match_collection, scorer_type, second_scorer_type);

  // MEMLEAK below causes seg fault (or used to)
  // free_match_collection(match_collection);

  // clean up
  free(psm_dir);
  free(protein_input_name);

  carp(CARP_INFO, "crux calculate q-value finished.");
  exit(0);
}

/*  ****************** Subroutines ****************/

/*
 */
static void print_text_files(
  MATCH_COLLECTION_T* match_collection,
  SCORER_TYPE_T scorer,
  SCORER_TYPE_T second_scorer
  ){

  // get filename and open file
  char* out_dir = get_string_parameter("output-dir");
  char* sqt_filename = get_string_parameter("qvalues-sqt-output-file");
  prefix_fileroot_to_name(&sqt_filename);
  char* tab_filename = get_string_parameter("qvalues-tab-output-file");
  prefix_fileroot_to_name(&tab_filename);
  BOOLEAN_T overwrite = get_boolean_parameter("overwrite");
  FILE* sqt_file = create_file_in_path( sqt_filename, out_dir, overwrite );
  FILE* tab_file = create_file_in_path( tab_filename, out_dir, overwrite );

  // print header
  int num_proteins = get_match_collection_num_proteins(match_collection);
  print_sqt_header(sqt_file, "target", num_proteins, TRUE);
  print_tab_header(tab_file);

  /*
  ALGORITHM_TYPE_T algorithm_type = get_algorithm_type_parameter("algorithm");
  char algorithm_str[64];
  algorithm_type_to_string(algorithm_type, algorithm_str);

  fprintf(sqt_file, "H\tComment\tmatches analyzed by %s\n", algorithm_str);
  */

  fprintf(sqt_file, "H\tComment\tmatches analyzed for q-values\n");

  // get match iterator sorted by spectrum
  MATCH_ITERATOR_T* match_iterator = 
    //new_match_iterator_spectrum_sorted(match_collection, scorer);
    new_match_iterator_spectrum_sorted(match_collection, XCORR);

  // print each spectrum only once, keep track of which was last printed
  int cur_spectrum_num = -1;
  int cur_charge = 0;
  int match_counter = 0;
  //  int max_matches = get_int_parameter("max-sqt-result");
  int max_matches = get_int_parameter("top-match");

  // for all matches
  while( match_iterator_has_next(match_iterator) ){

    // get match and spectrum
    MATCH_T* match = match_iterator_next(match_iterator);
    SPECTRUM_T* spectrum = get_match_spectrum(match);
    int this_spectrum_num = get_spectrum_first_scan(spectrum);
    int charge = get_match_charge(match);
    float spectrum_neutral_mass = get_spectrum_neutral_mass(spectrum, charge);
    float spectrum_precursor_mz = get_spectrum_precursor_mz(spectrum);
    int num_peptides = get_match_ln_experiment_size(match);
    num_peptides = expf(num_peptides);

    carp(CARP_DETAILED_DEBUG, 
         "SQT printing scan %i (current %i), charge %i (current %i)", 
         this_spectrum_num, cur_spectrum_num, charge, cur_charge);

    // if this spectrum has not been printed...
    if( cur_spectrum_num != this_spectrum_num
        || cur_charge != charge){

      carp(CARP_DETAILED_DEBUG, "Printing new S line");
      // print S line to sqt file
      cur_spectrum_num = this_spectrum_num;
      cur_charge = charge;

      print_spectrum_sqt(spectrum, sqt_file, num_peptides, charge);

      // print match to sqt file
      print_match_sqt(match, sqt_file, scorer, second_scorer);
      // print match to tab file
      print_match_tab(match, tab_file, this_spectrum_num, spectrum_precursor_mz,
                      spectrum_neutral_mass, num_peptides, charge, scorer);
      match_counter = 1;
    }
    // if this spectrum has been printed
    else{  
      if( match_counter < max_matches ){
        // print match to sqt file
        print_match_sqt(match, sqt_file, scorer, second_scorer);
        // print match to tab file
        print_match_tab(match, tab_file, this_spectrum_num, spectrum_precursor_mz,
                        spectrum_neutral_mass, num_peptides, charge, scorer);
        match_counter++;
      }
    }

  }// next match
  free_match_iterator(match_iterator);
  free(sqt_filename);
  free(tab_filename);
  free(out_dir);

}



/**
 * Compare doubles
 */
int compare_doubles_descending(
    const void *a,
    const void *b
    ){
  double temp = *((double *)a) - *((double *)b);
  if (temp > 0){
    return -1;
  } else if (temp < 0){
    return 1;
  } else {
    return 0;
  }
}


/**
 * Perform Benjamini-Hochberg qvalue calculations on p-values generated
 * as in Klammer et al. (In Press) for PSMs in psm_result_folder, searched
 * against the sequence database in fasta_file. Requires that the match 
 * collection objects in the psm_result_folder have been scored using 
 * the p-value method (for now, only LOGP_BONF_WEIBULL_XCORR). 
 * There should be no decoy data sets in the directory.
 * \returns a MATCH_COLLECTION object
 */
MATCH_COLLECTION_T* run_qvalue(
  char* psm_result_folder, 
  char* fasta_file 
  ){

  // double pi0 = get_double_parameter("pi0");
  MATCH_ITERATOR_T* match_iterator = NULL;
  MATCH_COLLECTION_T* match_collection = NULL;
  MATCH_T* match = NULL;

  // array to store out pvalues
  const int length = MAX_PSMS;
  double* pvalues = (double*) malloc(sizeof(double) * length);
  int num_psms = 0;
  
  // create MATCH_COLLECTION_ITERATOR_T object
  MATCH_COLLECTION_ITERATOR_T* match_collection_iterator =
    new_match_collection_iterator(psm_result_folder, fasta_file);
  
  while(match_collection_iterator_has_next(match_collection_iterator)){

    // get the next match_collection
    match_collection = 
      match_collection_iterator_next(match_collection_iterator);

    // create iterator
    match_iterator = new_match_iterator(match_collection, XCORR, FALSE);

    // for each match, get p-value    
    while(match_iterator_has_next(match_iterator)){
      match = match_iterator_next(match_iterator);
      pvalues[num_psms++] =  get_match_score(match, LOGP_BONF_WEIBULL_XCORR);
      if (num_psms >= MAX_PSMS){
        carp(CARP_ERROR, "Too many psms in directory %s", psm_result_folder);
      }
    }

    // ok free & update for next set
    free_match_iterator(match_iterator);
  }// next match collection

  free_match_collection_iterator(match_collection_iterator);

  // sort the - log p-values in descending order
  qsort(pvalues, num_psms, sizeof(double), compare_doubles_descending);

  double* qvalues = (double*) malloc(sizeof(double) * length);

  // work in negative log space, since that is where p- and qvalues end up
  double log_num_psms = - log(num_psms);
  double log_pi_0 = - log(get_double_parameter("pi0"));

  // convert the p-values into FDRs using Benjamini-Hochberg
  int idx;
  for (idx=0; idx < num_psms; idx++){
    carp(CARP_DETAILED_DEBUG, "pvalue[%i] = %.10f", idx, pvalues[idx]);
    int pvalue_idx = idx + 1; // start counting pvalues at 1
    double log_pvalue = pvalues[idx];

    double log_qvalue = 
      log_pvalue + log_num_psms - (-log(pvalue_idx)) + log_pi_0;
    qvalues[idx] = log_qvalue;
    carp(CARP_DETAILED_DEBUG, "no max qvalue[%i] = %.10f", idx, qvalues[idx]);
  }

  // convert the FDRs into q-values
  double max_log_qvalue = - BILLION;
  for (idx=num_psms-1; idx >= 0; idx--){
    if (qvalues[idx] > max_log_qvalue){
      max_log_qvalue = qvalues[idx];
    } else { // current q-value is <= max q-value
      // set to max q-value so far
      qvalues[idx] = max_log_qvalue; 
    }
    carp(CARP_DETAILED_DEBUG, "qvalue[%i] = %.10f", idx, qvalues[idx]);
  }

  // Iterate over the matches again
  match_collection_iterator = 
    new_match_collection_iterator(psm_result_folder, fasta_file);

  while(match_collection_iterator_has_next(match_collection_iterator)){

    // get the next match_collection
    match_collection = 
      match_collection_iterator_next(match_collection_iterator);

    // create iterator
    match_iterator = new_match_iterator(match_collection, XCORR, FALSE);

    // for each match, convert p-value to q-value
    while(match_iterator_has_next(match_iterator)){
      match = match_iterator_next(match_iterator);
      double log_pvalue = get_match_score(match, LOGP_BONF_WEIBULL_XCORR);
      carp(CARP_DETAILED_DEBUG, "- log pvalue  = %.6f", log_pvalue);
      
      // if p-value wasn't calculated, set q-value as nan
      if( log_pvalue == P_VALUE_NA ){
        set_match_score(match, LOGP_QVALUE_WEIBULL_XCORR, NaN() );
        continue;
      }

      // get the index of the p-value in the sorted list
      // FIXME slow, but it probably doesn't matter
      int idx;
      int pvalue_idx = -1;
      for (idx=0; idx < num_psms; idx++){
        double element = pvalues[idx];
        if ((element - EPSILON <= log_pvalue) &&
            (element + EPSILON >= log_pvalue)){
          pvalue_idx = idx; // start counting pvalues at 1
          break;
        }
      }
      
      set_match_score(match, LOGP_QVALUE_WEIBULL_XCORR, qvalues[pvalue_idx]);
    }

    // ok free & update for net set
    free_match_iterator(match_iterator);
    break; // just do the first match collection, which is the target matches
  }

   set_match_collection_scored_type(match_collection, 
       LOGP_QVALUE_WEIBULL_XCORR, TRUE);

  // free the match iterator, return match in sorted order of main_score type
  free_match_collection_iterator(match_collection_iterator);
  free(pvalues);
  free(qvalues);

  return match_collection;
}




/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */
