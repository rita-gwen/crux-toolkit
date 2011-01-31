/*************************************************************************//**
 * \file generate_peptides.cpp
 * AUTHOR: Chris Park
 * CREATE DATE: July 17 2006
 * \brief Given a protein fasta sequence database as input,
 * generate a list of peptides in the database that meet certain
 * criteria (e.g. mass, length, trypticity) as output. 
 ****************************************************************************/
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include "carp.h"
#include "peptide.h"
#include "peptide_src.h"
#include "protein.h"
#include "database.h"
#include "parse_arguments.h"
#include "parameter.h"
#include "index.h"

#include "XLinkPeptide.h"

#include "generate_peptides_iterator.h"

#include <string>



using namespace std;

static const int NUM_GEN_PEP_OPTIONS = 15;
static const int NUM_GEN_PEP_ARGS = 1;

/* Private function declarations */
void print_header();

int main(int argc, char** argv){

  /* Declarations */
  //int verbosity;
  BOOLEAN_T output_sequence;
  //  BOOLEAN_T print_trypticity = FALSE;
  BOOLEAN_T use_index;
  char* filename;
  
  //GENERATE_PEPTIDES_ITERATOR_T* peptide_iterator = NULL; 
  DATABASE_T* database = NULL;
  INDEX_T* index = NULL;
    
  /* Define optional command line arguments */ 
  int num_options = NUM_GEN_PEP_OPTIONS;
  const char* option_list[NUM_GEN_PEP_OPTIONS] = {
    "version",
    "verbosity",
    "parameter-file",
    "min-length",
    "max-length",
    "min-mass",
    "max-mass",
    "isotopic-mass",
    "enzyme", 
    "custom-enzyme", 
    "digestion", 
    //    "cleavages",
    "missed-cleavages",
    "unique-peptides",
    //"use-index",
    "output-sequence",
    //"output-trypticity",
    "sort"
  };

  /* Define required command-line arguments */
  const char* argument_list[] = { 
    "protein database",
    "link sites",
    "link mass"};

  int num_arguments = sizeof(argument_list) / sizeof(char*);

  //TODO make this a debug flag
  //set_verbosity_level(CARP_DETAILED_DEBUG);
  set_verbosity_level(CARP_ERROR);

  /* Prepare parameter.c to read command line, set default option values */
  initialize_parameters();

  /* Set optional and required command-line arguments */
  select_cmd_line_options( option_list, num_options );
  select_cmd_line_arguments( argument_list, num_arguments );


  /* Parse the command line, including optional params file
     includes syntax, type, and bounds checks and dies on error */
  parse_cmd_line_into_params_hash(argc, argv, "crux-generate-xlink-peptides");

  /* Set verbosity */
  //verbosity = get_int_parameter("verbosity");
  //set_verbosity_level(verbosity);

  /* Get parameter values */
  //  print_trypticity = get_boolean_parameter("output-trypticity");
  output_sequence = get_boolean_parameter("output-sequence");
  filename = get_string_parameter("protein database");
  //use_index = get_boolean_parameter("use-index");
  use_index = is_directory(filename);
  if( use_index == TRUE ){
    index = new_index_from_disk(filename);//, 
  }else{
    database = new_database(filename, FALSE); // not memmapped
  }
  free(filename);

  /* Generate peptides and print to stdout */

  // get list of mods
  PEPTIDE_MOD_T** peptide_mods = NULL;
  int num_peptide_mods = generate_peptide_mod_list( &peptide_mods );
  carp(CARP_DEBUG, "Got %d peptide mods", num_peptide_mods);



  print_header(); // TODO: add mods info

  XLinkPeptide::setLinkerMass(get_double_parameter("link mass"));
  
  


  XLinkBondMap bondmap;

  MatchCandidateVector xlink_candidates(
    bondmap, 
    peptide_mods, 
    num_peptide_mods,
    index,
    database);


  int type_counts[3] = {0,0,0};

  int num_inter = 0;
  int num_intra = 0;
  int num_inter_intra = 0;

  for (unsigned int idx=0;idx<xlink_candidates.size();idx++) {

    MatchCandidate* current_candidate = xlink_candidates[idx];

    FLOAT_T mono_mass = current_candidate->getMass(MONO);
    FLOAT_T average_mass = current_candidate->getMass(AVERAGE);
    int num_missed_cleavages = current_candidate->getNumMissedCleavages();
    string sequence = current_candidate->getSequenceString();
    MATCHCANDIDATE_TYPE_T candidate_type = current_candidate->getCandidateType();

    type_counts[candidate_type]++;

    string stype;
    switch(candidate_type) {

      case LINEAR_CANDIDATE:
        stype = "Linear";
        break;
      case SELFLOOP_CANDIDATE:
        stype = "SelfLoop";
        num_intra++;
        break;
      case XLINK_CANDIDATE:
        stype = "XLink";
        XLinkPeptide* xlink_peptide = (XLinkPeptide*)current_candidate;

        if (xlink_peptide->isInter() && xlink_peptide->isIntra()) {
          stype += "(Intra/Inter)";
          num_inter_intra++;
        } else if (xlink_peptide->isInter()) {
          stype += "(Inter)";
          num_inter++;
        } else if (xlink_peptide->isIntra()) {
          num_intra++;
        } else {
          carp(CARP_FATAL,"????");
        }
    
    }


    printf("%f\t%f\%i\t%s\t%s\n",
      mono_mass, 
      average_mass, 
      num_missed_cleavages, 
      sequence.c_str(), 
      stype.c_str());

  }

  // debug purpose
  carp(CARP_INFO, "total candidates: %d", xlink_candidates.size());
  carp(CARP_INFO, "Number Linear:%d", type_counts[LINEAR_CANDIDATE]);
  carp(CARP_INFO, "Number SelfLoop:%d", type_counts[SELFLOOP_CANDIDATE]);
  carp(CARP_INFO, "Number XLinks:%d", type_counts[XLINK_CANDIDATE]);
  carp(CARP_INFO, "Number Inter Links:%d", num_inter);
  carp(CARP_INFO, "Number Intra Links:%d", num_intra);
  carp(CARP_INFO, "Number Inter/Intra:%d", num_inter_intra);


  free_parameters();

  /* successfull exit message */
  carp(CARP_INFO, "crux-generate-peptides finished.");

  exit(0);
}

void print_header(){
  BOOLEAN_T bool_val;

  char* database_name = get_string_parameter("protein database");
  printf("# PROTEIN DATABASE: %s\n", database_name);

  printf("# OPTIONS:\n");
  printf("#\tmin-mass: %.2f\n", get_double_parameter("min-mass"));
  printf("#\tmax-mass: %.2f\n", get_double_parameter("max-mass"));
  printf("#\tmin-length: %d\n", get_int_parameter("min-length"));
  printf("#\tmax-length: %d\n", get_int_parameter("max-length"));
  printf("#\tenzyme: %s\n", get_string_parameter_pointer("enzyme"));
  printf("#\tdigestion: %s\n", get_string_parameter_pointer("digestion"));
  //printf("#\tcleavages: %s\n", get_string_parameter_pointer("cleavages"));
  
  bool_val = get_boolean_parameter("missed-cleavages");
  printf("#\tallow missed-cleavages: %s\n", boolean_to_string(bool_val));
  printf("#\tsort: %s\n",  get_string_parameter_pointer("sort"));
  printf("#\tisotopic mass type: %s\n", 
         get_string_parameter_pointer("isotopic-mass"));
  printf("#\tverbosity: %d\n", get_verbosity_level());

  //bool_val = get_boolean_parameter("use-index");
  bool_val = is_directory(database_name);
  printf("#\tuse index: %s\n", boolean_to_string(bool_val));
  //get_string_parameter_pointer("use-index"));
  
  AA_MOD_T** aa_mod_list = NULL;
  int num_aa_mods = get_all_aa_mod_list(&aa_mod_list);
  int mod_idx = 0;
  for(mod_idx=0; mod_idx < num_aa_mods; mod_idx++){
    printf("#\tmodification: ");
    char* mod_str = aa_mod_to_string(aa_mod_list[mod_idx]);
    printf("%s\n", mod_str);
    free(mod_str);
  }
  free(database_name);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */