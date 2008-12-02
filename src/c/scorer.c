/*************************************************************************//**
 * \file scorer.c
 * AUTHOR: Chris Park
 * CREATE DATE: 9 Oct 2006
 * DESCRIPTION: object to score spectrum vs. spectrum or spectrum
 * vs. ion_series 
 * REVISION: $Revision: 1.66 $
 ****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include "objects.h"
#include "ion_series.h"
#include "crux-utils.h"
#include "spectrum.h"
#include "scorer.h"
#include "parameter.h"
#include "unistd.h"


// the bin width(Sp)
#define bin_width_mono 1.0005079
#define bin_width_average 1.0011413

// cross correlation offset range(Xcorr)
#define MAX_XCORR_OFFSET 75

// Constants for EVD p_value calculation
// These constants are hardware dependent.
// These values should be good for double precision floating point
// numbers compatible with the IEEE 754 standard.
#define DBL_EPSILON  2.2204460492503131e-16
#define DBL_MAX_10_EXP 308

#define BONFERRONI_CUT_OFF_P 0.0001
#define BONFERRONI_CUT_OFF_NP 0.01

#define GMTK_MAX_ION_FILES 50
#define GMTK_NUM_CHARGES 2
// FIX !! check different charges
#define GMTK_NUM_BASE_IONS 3
#define GMTK_NUM_NEUTRAL_LOSS 2
#define GMTK_NUM_ION_SERIES \
  GMTK_NUM_BASE_IONS * GMTK_NUM_CHARGES * (GMTK_NUM_NEUTRAL_LOSS + 1)

#define GMTK_NUM_PAIRED_ION_SERIES 15

/**
 * \struct scorer
 * \brief An object to score spectrum v. spectrum or spectrum v. ion_series
 */
struct scorer {
  SCORER_TYPE_T type; ///< The type of scorer
  float sp_beta; ///< used for Sp: the beta variable 
  float sp_max_mz; ///< used for Sp: the max mz for the intensity array
  int sp_b_y_ion_matched; ///< The most recent ion_collection number of the b, y ion matched while scoring for SP
  int sp_b_y_ion_possible; ///< The most recent ion_collection number of the b, y ion possible while scoring for SP
  float sp_b_y_ion_fraction_matched; ///< The ratio of matched and possible.

  float* intensity_array; ///< used for Sp: the intensity array, which can be indexed using the m/z
  float max_intensity; ///< the max intensity in the intensity array
  BOOLEAN_T initialized; ///< has the scorer been initialized?
  int last_idx; ///< the last index in the array, the data size of the array

  /// used for xcorr
  float* observed; ///< used for Xcorr: observed spectrum intensity array
  float* theoretical; ///< used for Xcorr: theoretical spectrum intensity array
};

// defined later
void add_intensity(
  float* intensity_array, ///< the intensity array to add intensity at index add_idx -out
  int add_idx,            ///< the idex to add the intensity -in
  float intensity         ///< the intensity to add -in
  );

/**
 *\returns An (empty) scorer object.
 */
SCORER_T* allocate_scorer(void){
  SCORER_T* scorer = (SCORER_T*)mycalloc(1, sizeof(SCORER_T));
  return scorer;
}

/**
 * If not planning to use the default values, must parse the parameter file before.
 * Instantiates a new scorer object from a filename. 
 * \returns a new scorer object
 */
SCORER_T* new_scorer(
  SCORER_TYPE_T type ///< the type of scorer -in
  )
{
  // get allocated scorer
  SCORER_T* scorer = allocate_scorer();
  
  // set score type
  scorer->type = type;
  
  // set fields needed for each score type
  if(type == SP){
    scorer->sp_beta = get_double_parameter("beta");
    scorer->sp_max_mz = get_double_parameter("max-mz");
    // allocate the intensity array
    scorer->intensity_array = (float*)mycalloc(scorer->sp_max_mz, sizeof(float));
    scorer->max_intensity = 0;
    scorer->last_idx = 0;
    // the scorer as not been initialized yet.
    scorer->initialized = FALSE;
  }
  else if(type == XCORR){
    // scorer->sp_max_mz = get_double_parameter("max-mz");
    // scorer->observed = (float*)mycalloc((int)scorer->sp_max_mz, sizeof(float));
    scorer->last_idx = 0;
    // the scorer as not been initialized yet.
    scorer->initialized = FALSE;
  }
  else if(type == LOGP_EXP_SP || type == LOGP_BONF_EXP_SP){
    // the scorer does not need to be initialized for logp_exp_sp.
    scorer->initialized = TRUE;
  }
  else{
    // the scorer as not been initialized yet.
    scorer->initialized = FALSE;
  }

  return scorer;
}

/**
 * Frees an allocated scorer object.
 */
void free_scorer(
  SCORER_T* scorer ///< the ion collection to free - in
  )
{
  // score type SP?
  if(scorer->type == SP){
    free(scorer->intensity_array);
  }
  else if(scorer->type == XCORR){
    free(scorer->observed);
  }

  free(scorer);
}


/**
 * normalize array so that maximum peak equals threshold
 */
void nomalize_intensity_array(
  float* intensity_array, ///< the array to normalize -in/out
  int array_size, ///< size of array -in
  float max_intensity, ///< the maximum intensity in array -in
  float threshold   ///< the threshold to which the peaks should be normalized -in
  )
{
  int mz_idx = 0;

  // return if max_intensity is 0
  if(max_intensity < 0.00001){
    return;
  }

  // normalize all peaks
  for(; mz_idx < array_size; ++mz_idx){
    // fprintf(stderr, "%.4f", intensity_array[mz_idx]);
    intensity_array[mz_idx] = intensity_array[mz_idx] * threshold / max_intensity;

    // DEBUG
    // carp(CARP_INFO, "norm data[%d] = %.4f, max_intensity: %.4f",mz_idx, intensity_array[mz_idx], max_intensity); 
  }
}

/**
 * smooth all peaks in intensity array
 * Replaces the original array with the newly smooothed array
 */
void smooth_peaks(
  SCORER_T* scorer        ///< the scorer object -in/out
  )
{
  int idx = 2;
  float* array = scorer->intensity_array;

  // create a new array, which will replace the original intensity array
  float* new_array = (float*)mycalloc(scorer->sp_max_mz, sizeof(float));

  switch (scorer->type){
    case SP:
      // iterate over all peaks
      for(; idx < (int)scorer->sp_max_mz-2; ++idx){
        // smooooth
        new_array[idx] = (array[idx-2]+4*array[idx-1]+6*array[idx]+4*array[idx+1]+array[idx+2])/16;

        // DEBUG
        // carp(CARP_INFO, "smooth data[%d] = %f",idx, new_array[idx]); 
        
        // set last idx in the array
        if(scorer->last_idx < idx && new_array[idx] == 0){
          scorer->last_idx = idx -1;
          break;
        }
      }
      break;
      
    case XCORR:
    case DOTP:
    case LOGP_EXP_SP:
    case LOGP_BONF_EXP_SP:
    case LOGP_WEIBULL_SP:
    case LOGP_BONF_WEIBULL_SP:
    case LOGP_WEIBULL_XCORR:
    case LOGP_BONF_WEIBULL_XCORR:
    case LOGP_EVD_XCORR:
    case LOGP_BONF_EVD_XCORR:
    case Q_VALUE:
    case PERCOLATOR_SCORE:  
    case LOGP_QVALUE_WEIBULL_XCORR:  
      break;
  }
  free(scorer->intensity_array);
  scorer->intensity_array = new_array;
}

/**
 * get the mean of intensity in array within +/- 50 mz of the working peak
 * \returns the mean +/- 50mz region
 */
float get_mean_from_array(
  float* original_array, ///< the array to normalize -in
  int array_size, ///< the size of array -in
  int peak_idx,  ///< the peak indx in array -in
  int* peak_count ///< pointer to peak count, store peak count here -out
  )
{
  float total_intensity = 0;
  int start_idx = peak_idx - 50;
  int end_idx = peak_idx + 50;

  // set upper bound
  if(peak_idx + 50 >= array_size){
    end_idx = array_size-1;
  }
  // set start index
  if(peak_idx - 50 <= 0){
    start_idx = 0;
  }
  
  // sum up the intensities
  for(; start_idx <= end_idx; ++start_idx){
    // printf("%.2f\n", original_array[start_idx]);

    ++*peak_count;
    total_intensity += original_array[start_idx];
  }
  
  // BUG! it should divide by 101 but Sequest uses 100
  return (total_intensity / (*peak_count-1));
}

/**
 * get the stdev of intensity in array within +/- 50 mz of the working peak
 * \returns the stdev +/- 50mz region
 */
float get_stdev_from_array(
  float* original_array, ///< the array to normalize -in
  int array_size, ///< the size of array -in
  int peak_idx,  ///< the peak indx in array -ina
  float mean,     ///< the mean in the +/- 50 interval -in
  int peak_count ///<  peak count -in
  )
{
  float variance = 0;
  int start_idx = peak_idx - 50;
  int end_idx = peak_idx + 50;
  float dev = 0;

  // set upper bound
  if(peak_idx + 50 >= array_size){
    end_idx = array_size-1;
  }
  // set start index
  if(peak_idx - 50 <= 0){
    start_idx = 0;
  }
  
  // sum up the intensities
  for(; start_idx <= end_idx; ++start_idx){
    // sum up all deviations squared
    dev = original_array[start_idx] - mean;
    variance += (dev*dev);
  }
  
  // return the stdev
  return sqrt(variance/peak_count);
}

/***
 * zero and extract peaks
 * extract peaks that are larger than mean + #step*stdev into new array
 * zero out the peaks that have been extracted
 * yes, the facter that a peak has removed will effect the fallowing peaks
 */
void zero_peak_mean_stdev(
  SCORER_T* scorer,        ///< the scorer object -in/out
  float* original_array, ///< the array to normalize -in/out
  float* new_array, ///< the array to normalize -in/out                          
  int step                ///< is this 1 or 2 step -in
  )
{
  int peak_count = 0;
  int idx = 0;
  int array_size = (int)scorer->sp_max_mz;
  float mean = 0;
  float stdev = 0;

  // iterate over all peaks
  for(; idx < array_size; ++idx){
    peak_count = 0;
    // get mean
    mean = get_mean_from_array(original_array, array_size, idx, &peak_count);
    // get stdev
    stdev = get_stdev_from_array(original_array, array_size, idx, mean, peak_count);
    
    // DEBUG
    // carp(CARP_INFO, "zero idx: %d mean: %.8f, stdev: %.8f", idx, mean, stdev);
    
    // iterate over all positions and extract peaks
    if(original_array[idx] > (mean + step*stdev)){
      new_array[idx] = original_array[idx] - (mean - stdev);
               
      // DEBUG
      // carp(CARP_INFO, "extract peak: %.2f at idx: %d", original_array[idx], idx);
      
      // reset the last idx
      if(scorer->last_idx < idx){
        scorer->last_idx = idx;
      }
      
      // for only step 1,
      if(step == 1){
        // zero out original peak
        original_array[idx] = 0;
      }
    }
  }
}

/**
 *  zero and extract peaks
 * extract peaks that are larger than mean + #step*stdev into new array
 * zero out the peaks that have been extracted
 * repeat twice, than replace old array with extracted peak array
 */
void zero_peaks(
  SCORER_T* scorer   ///< the scorer object -in/out
  )
{
  // create a new array, which will replace the original intensity array
  float* new_array = (float*)mycalloc(scorer->sp_max_mz, sizeof(float));
  
  // step 1,
  zero_peak_mean_stdev(scorer, scorer->intensity_array, new_array, 1);
  // step 2,
  zero_peak_mean_stdev(scorer, scorer->intensity_array, new_array, 2);
  
  // replace intensity_array with new intensity array
  free(scorer->intensity_array);
  scorer->intensity_array = new_array;

  // DEBUG
  /*
  int idx = 0;
  for(; idx < scorer->sp_max_mz; ++idx){
    carp(CARP_INFO, "extracted peaks: %.2f, at idx %d", new_array[idx], idx);
  }
  */
}

/**
 * keep only the peaks up to top rank peaks remove other peaks.
 * do second normalization on the top peaks back to max 100 intensity
 * replace old array with normalized top peak array
 */
void extract_peaks(
  SCORER_T* scorer,        ///< the scorer object -in/out
  int top_rank  ///< keep the top ranking peaks -in
  )
{
  // create a new array, which will replace the original intensity array
  float* temp_array = (float*)mycalloc((int)scorer->sp_max_mz, sizeof(float));
  float* original_array = scorer->intensity_array;
  int idx = 0;
  int temp_idx = 0;
  float cut_off = 0;
  float max_intensity;

  // copy all peaks to temp_array
  for(; idx < (int)scorer->sp_max_mz; ++idx){
    if(scorer->intensity_array[idx] > 0){
      temp_array[temp_idx] = original_array[idx];

      // DEBUG print all temp array values
      // carp(CARP_INFO, "before sort data[%d]=%.3f",temp_idx, temp_array[temp_idx]);
      
      ++temp_idx;
    }
  }
  
  // if there's over top_rank peaks, keep only top_rank peaks
  // quick sort
  quicksort(temp_array, temp_idx);
  
  // set max and cut_off
  max_intensity = temp_array[0];
  cut_off = temp_array[top_rank-1];
  
  // remove peaks bellow cut_off 
  // also, normalize peaks to max_intensity to 100
  for(idx = 0; idx < (int)scorer->sp_max_mz; ++idx){
    // DEBUG print all temp array values
    // carp(CARP_INFO, "sorted data[%d]=%.3f",idx, temp_array[idx]);

    if(original_array[idx] > 0){
      // is it bellow cut off?
      if(original_array[idx] < cut_off){
        // remove peak
        original_array[idx] = 0;
      }
      else{
        // nomalize peak to max 100
        original_array[idx] = original_array[idx] / max_intensity * 100;
      }
    }
  }
  
  free(temp_array);
  return;
}
 
/**
 * equalize all peaks in a continous region to the largest peak within the continous bins
 * start from left to right
 */
void equalize_peaks(
  SCORER_T* scorer ///< the scorer object -in/out
  )
{
  int idx;
  // int array_size = (int)scorer->sp_max_mz-2;
  
  float max_intensity = 0;
  int end_idx = 0;
  int last_idx = scorer->last_idx;

  // equalize peaks to it's greatest intensity
  // should use array size, but sequest seems to have a bug
  // last idx is thus, modification to fit sequest
  // consequences are we will not equalize the very last peak.
  for(idx = 0; idx < last_idx/*array_size*/; ++idx){
    // are we inside a continous block?
    if(scorer->intensity_array[idx] > 0){
      max_intensity = scorer->intensity_array[idx];
      end_idx = idx + 1;
      
      // loop to find the largest peak in the continous block
      while(end_idx < last_idx && scorer->intensity_array[end_idx] > 0){
        // reset max intensity
        if(scorer->intensity_array[end_idx] > max_intensity){
           max_intensity = scorer->intensity_array[end_idx];
        }
        
        ++end_idx;
      }
      
      // set all peaks in block to max_intesity
      for(; idx < end_idx; ++idx){
        scorer->intensity_array[idx] = max_intensity;
      }
    }
  }
}
    
/**
 * create the intensity array
 * SCORER must have been created for SP type
 * \returns TRUE if successful, else FLASE
 */
BOOLEAN_T create_intensity_array_sp(
  SPECTRUM_T* spectrum,    ///< the spectrum to score -in
  SCORER_T* scorer,        ///< the scorer object -in/out
  int charge               ///< the peptide charge -in 
  )
{
  PEAK_T* peak = NULL;
  PEAK_ITERATOR_T* peak_iterator = NULL;
  float peak_location = 0;
  float max_intensity = 0;
  int mz = 0;
  float intensity = 0;
  // FIXME, later be able pick between average and mono
  float bin_width = bin_width_mono;
  float precursor_mz = get_spectrum_precursor_mz(spectrum);
  float experimental_mass_cut_off = precursor_mz*charge + 50;
  int top_bins = 200;

  // DEBUG
  // carp(CARP_INFO, "precursor_mz: %.1f", precursor_mz);
  
  // if score type equals SP
  if(scorer->type != SP){
    carp(CARP_ERROR, "incorrect scorer type, only use this method for SP scorers");
    return FALSE;
  }
  
  // check bin width type
  // DO this at some time!!

  // create a peak iterator
  peak_iterator = new_peak_iterator(spectrum);
  
  // while there are more peaks to iterate over..
  while(peak_iterator_has_next(peak_iterator)){
    peak = peak_iterator_next(peak_iterator);
    peak_location = get_peak_location(peak);
    
    // skip all peaks larger than experimental mass
    if(peak_location > experimental_mass_cut_off){
      continue;
    }
    
    // skip all peaks within precursor ion mz +/- 15
    if(peak_location < precursor_mz + 15 &&  peak_location > precursor_mz - 15){
      continue;
    }
    
    // map peak location to bin
    mz = (int)(peak_location/bin_width + 0.5);
    
    // get intensity
    intensity = sqrt(get_peak_intensity(peak));
    
    // set intensity in array with correct mz, only if max peak in the bin
    if(scorer->intensity_array[mz] < intensity){
      scorer->intensity_array[mz] = intensity;
      
      // check if max_intensity
      if(intensity > max_intensity){
        max_intensity = intensity;
      }
    }
    
    // set last idx to the largest added peak mz value
    if(scorer->last_idx < mz){
      scorer->last_idx = mz;
    }
  }

  // set max_intensity
  scorer->max_intensity = max_intensity;
  
  // DEBUG!!
  // carp(CARP_INFO, "exp_cut_off: %.2f max_intensity: %.2f", experimental_mass_cut_off, (max_intensity*max_intensity));

  
  // normalize intensity
  nomalize_intensity_array(scorer->intensity_array, scorer->last_idx+1, scorer->max_intensity, 100);
  
  // smooth peaks
  smooth_peaks(scorer);

  // zero peaks
  zero_peaks(scorer);
  
  // Sequest28 modifications.
  // Determine number of top peaks to select based on the experimental mass
  // In Sequest27, the top peaks were always selected as 200.
  // keep top ions of square-root(16*experimental mass) ranking, but not exceeding 200 ions
  /*
  if(experimental_mass_cut_off-50 < 3200){
    // top bins are sqrt of 16* experimental mass
    top_bins = (int)(sqrt((experimental_mass_cut_off-50)*16) + 0.5);    
    // however cannot exceed 200
    if(top_bins > 200){
      top_bins = 200;
    }
  }
  else{
    top_bins = int((experimental_mass_cut_off-50)/14.00);
  }
  */

  // extrace the top ions
  extract_peaks(scorer, top_bins);

  // equalize peaks
  equalize_peaks(scorer);

  // Debug
  /*
  int idx; int count = 0;
  for(idx = 0; idx < scorer->last_idx; ++idx){
    if(scorer->intensity_array[idx] > 0){
      // DEBUG
      carp(CARP_INFO, "scoring array[%d], %d = %.4f", idx, count, scorer->intensity_array[idx]);
      ++count;
    }
  }
  */
  
  // free peak iterator
  free_peak_iterator(peak_iterator);
  
  // scorer now been initialized!, ready to score peptides..
  scorer->initialized = TRUE;

  return TRUE;
}

/**
 * calculates all the necessay values for Sp score, related to the specfic ion_type
 * adds to intensity_sum and repeat_count
 *\returns the number of matches found from the predicted ions
 */
int calculate_ion_type_sp(
  SCORER_T* scorer,        ///< the scorer object -in                          
  ION_SERIES_T* ion_series, ///< the ion series to score against the spectrum -in
  float* intensity_sum,     ///< the total intensity sum of all matches so far -out
  ION_TYPE_T ion_type,      ///< the ion type to check -in
  int* repeat_count         ///< the repeated count of ions (ex. consecutive b ions) -out
  )
{
  int cleavage_idx = 0;
  ION_T* ion = NULL;
  float one_intensity = 0;
  int ion_match = 0;
  int ion_charge = 0;
  int intensity_array_idx = 0;

  int* before_cleavage = (int*)mymalloc(get_ion_series_charge(ion_series)*sizeof(int));
  int cleavage_array_idx = 0;

  // initialize before cleavage indecies
  for(; cleavage_array_idx < get_ion_series_charge(ion_series); ++cleavage_array_idx){
    before_cleavage[cleavage_array_idx] = -1;
  }
  
  // create ion constraint
  ION_CONSTRAINT_T* ion_constraint = 
    new_ion_constraint(get_ion_constraint_mass_type(get_ion_series_ion_constraint(ion_series)), get_ion_series_charge(ion_series), ion_type, FALSE);
  
  // create the filtered iterator that will select among the ions
  ION_FILTERED_ITERATOR_T* ion_iterator = new_ion_filtered_iterator(ion_series, ion_constraint);
  
  // while there are ion's in ion iterator, add matched observed peak intensity
  while(ion_filtered_iterator_has_next(ion_iterator)){
    ion = ion_filtered_iterator_next(ion_iterator);
    intensity_array_idx = (int)(get_ion_mass_z(ion)/bin_width_mono + 0.5);
    // get the intensity matching to ion's m/z
    if(intensity_array_idx < scorer->sp_max_mz){
      one_intensity = scorer->intensity_array[intensity_array_idx];
    }
    else{
      // printf("index out of bounds: %d scorer->sp_max_mz: %.2f ion_mass_z: %.2f\n", intensity_array_idx, scorer->sp_max_mz, get_ion_mass_z(ion));
      one_intensity = 0;
    }

    // if there is a match in the observed spectrum
    if(one_intensity > 0){
      // int idx = (int)(get_ion_mass_z(ion)/bin_width_mono + 0.5);
      // carp(CARP_INFO, "idx = %d\n", idx);
  
      // DEBUG
      // carp(CARP_INFO, "matched ion: %.2f ion intensity: %.2f", get_ion_mass_z(ion), one_intensity);

      ++ion_match;
      *intensity_sum = *intensity_sum + one_intensity;
      
      // get ion charge
      ion_charge = get_ion_charge(ion) - 1;
      
      // check if repeated ion b1, b2, ...
      if((cleavage_idx = get_ion_cleavage_idx(ion)) == before_cleavage[ion_charge] + 1){
        ++*repeat_count;
      }
      
      // reset the previous cleavage index 
      before_cleavage[ion_charge] = cleavage_idx;
    }
  }
  
  
  // free ion iterator, ion_constraint
  free(before_cleavage);
  free_ion_constraint(ion_constraint);
  free_ion_filtered_iterator(ion_iterator);

  return ion_match;
}


/**
 * given a spectrum and ion series calculates the Sp score
 *\returns the sp score 
 */
float gen_score_sp(
  SCORER_T* scorer,        ///< the scorer object -in
  SPECTRUM_T* spectrum,    ///< the spectrum to score -in
  ION_SERIES_T* ion_series ///< the ion series to score against the spectrum -in
  )
{
  float final_score = 0;
  float intensity_sum = 0;
  int ion_match = 0;
  int repeat_count = 0;
  
  // initialize the scorer before scoring if necessary
  if(!scorer->initialized){
    // create intensity array
    if(!create_intensity_array_sp(spectrum, scorer, get_ion_series_charge(ion_series))){
      carp(CARP_ERROR, "failed to produce Sp");
      free(spectrum);
      free(ion_series);
      free(scorer);
      exit(1);
    }
  }

  // calculate the B_ION and Y_ION portions of the Sp score
  ion_match = 
    calculate_ion_type_sp(scorer, ion_series, &intensity_sum, B_ION, &repeat_count) +
    calculate_ion_type_sp(scorer, ion_series, &intensity_sum, Y_ION, &repeat_count);
  
  // set the fraction of  b,y ions matched for this ion_series
  scorer->sp_b_y_ion_matched  = ion_match;
  scorer->sp_b_y_ion_possible = get_ion_series_num_ions(ion_series);
  scorer->sp_b_y_ion_fraction_matched = (float)ion_match / get_ion_series_num_ions(ion_series);

  //// DEBUG!!!!
  /*
  carp(CARP_INFO, "# repeat count: %d ion_match count: %d, total_ion count: %d sum: %.2f", 
       repeat_count, ion_match, get_ion_series_num_ions(ion_series),
       intensity_sum);
  */
  // calculate Sp score.
  if(ion_match != 0){
    final_score = 
      (intensity_sum * ion_match) * (1+ (repeat_count * scorer->sp_beta)) / get_ion_series_num_ions(ion_series);
  }
  
  // return score
  return final_score;
}





/*****************************************************
 * Xcorr related functions
 * 
 *****************************************************/

/**
 * normalize each 10 regions of the observed spectrum to max 50
 */
void normalize_each_region(
  SCORER_T* scorer,        ///< the scorer object -in/out
  float* max_intensity_per_region, ///< the max intensity in each 10 regions -in
  int region_selector ///< the size of each regions -in
  )
{
  int bin_idx = 0;
  int region_idx = 0;
  float max_intensity = max_intensity_per_region[region_idx];
  
  // normazlie each region
  for(; bin_idx < scorer->sp_max_mz; ++bin_idx){
    if(bin_idx >= region_selector*(region_idx+1) && region_idx < 9){
      ++region_idx;
      max_intensity = max_intensity_per_region[region_idx];;
    }

    // don't normalize if no peaks in region
    if(max_intensity != 0){
      // normalize intensity to max 50
      scorer->observed[bin_idx] = (scorer->observed[bin_idx] / max_intensity) * 50;
      
      // DEBUG
      // carp(CARP_INFO, "bin: %d, region idx: %d, obsered_mz: %.2f", bin_idx, region_idx, scorer->observed[bin_idx]);
    }

    // no more peaks beyong the 10 regions mark, exit out
    if(bin_idx > 10*region_selector){
      return;
    }
  }
}

/**
 * create the intensity arrays for observed spectrum
 * SCORER must have been created for XCORR type
 * \returns TRUE if successful, else FLASE
 */
BOOLEAN_T create_intensity_array_observed(
  SCORER_T* scorer,        ///< the scorer object -in/out
  SPECTRUM_T* spectrum,    ///< the spectrum to score(observed) -in
  int charge               ///< the peptide charge -in 
  )
{  
  PEAK_T* peak = NULL;
  PEAK_ITERATOR_T* peak_iterator = NULL;
  float peak_location = 0;
  int mz = 0;
  float intensity = 0;
  float bin_width = bin_width_mono;
  float precursor_mz = get_spectrum_precursor_mz(spectrum);
  float experimental_mass_cut_off = precursor_mz*charge + 50;

  // set max_mz and malloc space for the observed intensity array
  if(experimental_mass_cut_off > 512){
    int x = (int)experimental_mass_cut_off / 1024;
    float y = experimental_mass_cut_off - (1024 * x);
    scorer->sp_max_mz = x * 1024;

    if(y > 0){
      scorer->sp_max_mz += 1024;
    }
  }
  else{
    scorer->sp_max_mz = 512;
  }

  // DEBUG
  // carp(CARP_INFO, "experimental_mass_cut_off: %.2f sp_max_mz: %.3f", experimental_mass_cut_off, scorer->sp_max_mz);
  scorer->observed = (float*)mycalloc((int)scorer->sp_max_mz, sizeof(float));
  
  // store the max intensity in each 10 regions to later normalize
  float* max_intensity_per_region = (float*)mycalloc(10, sizeof(float));
  int region_selector = (int)(get_spectrum_max_peak_mz(spectrum) / 10);

  // DEBUG
  // carp(CARP_INFO, "max_peak_mz: %.2f, region size: %d",get_spectrum_max_peak_mz(spectrum), region_selector);
  
  int region = 0;
  // create a peak iterator
  peak_iterator = new_peak_iterator(spectrum);
  
  // while there are more peaks to iterate over..
  while(peak_iterator_has_next(peak_iterator)){
    peak = peak_iterator_next(peak_iterator);
    peak_location = get_peak_location(peak);
    
    // skip all peaks larger than experimental mass
    if(peak_location > experimental_mass_cut_off){
      continue;
    }
    
    // skip all peaks within precursor ion mz +/- 15
    if(peak_location < precursor_mz + 15 &&  peak_location > precursor_mz - 15){
      continue;
    }
    
    // map peak location to bin
    mz = (int)(peak_location / bin_width + 0.5);
    region = mz / region_selector;

    // don't let index beyond array
    if(region > 9){
      continue;
      // region = 9;
    }

    // get intensity
    // sqrt the original intensity
    intensity = sqrt(get_peak_intensity(peak));
           
    // set intensity in array with correct mz, only if max peak in the bin
    if(scorer->observed[mz] < intensity){
      scorer->observed[mz] = intensity;
            
      // check if this peak is max intensity in the region(one out of 10)
      if(max_intensity_per_region[region] < intensity){
        max_intensity_per_region[region] = intensity;
      }
    }    
  }

  
  // DEBUG
  /*
  int i = 0;
  for(; i < 10; i++){
    carp(CARP_INFO, "High intensity bin %d: %.2f", i, max_intensity_per_region[i]);
  }
  */

  // normalize each 10 regions to max intensity of 50
  normalize_each_region(scorer, max_intensity_per_region, region_selector);
  
  // DEBUG
  /*
  i = 0;
  for(; i < scorer->sp_max_mz; i++){
    carp(CARP_INFO, "Intensity array[%d]: %.2f", i, scorer->observed[i]);
  } */

  // TODO maybe replace with a faster implementation that uses cum distribution
  float* new_observed = (float*)mycalloc((int)scorer->sp_max_mz, sizeof(float));
  int idx;
  for (idx=0; idx < scorer->sp_max_mz; idx++){
    new_observed[idx] = scorer->observed[idx];
    int sub_idx;
    for (sub_idx=idx - MAX_XCORR_OFFSET; sub_idx <= idx + MAX_XCORR_OFFSET;
        sub_idx++){
      if (sub_idx <= 0 || sub_idx >= scorer->sp_max_mz){
        continue;
      }
      new_observed[idx] -= (scorer->observed[sub_idx] / (MAX_XCORR_OFFSET * 2.0) );
    }
  }

  free(scorer->observed);
  scorer->observed = new_observed;

  // free heap
  free(max_intensity_per_region);
  free_peak_iterator(peak_iterator);

  return TRUE;
}

/**
 * create the intensity arrays for theoretical spectrum
 * SCORER must have been created for XCORR type
 * \returns TRUE if successful, else FLASE
 */
BOOLEAN_T create_intensity_array_theoretical(
  SCORER_T* scorer,        ///< the scorer object -in/out
  ION_SERIES_T* ion_series, ///< the ion series to score against the spectrum(theoretical) -in
  float* theoretical       ///< the empty theoretical spectrum -out
  )
{
  ION_T* ion = NULL;
  int intensity_array_idx = 0;
  ION_TYPE_T ion_type;
  float bin_width = bin_width_mono;
  // int charge = get_ion_series_charge(ion_series);
  // create the ion iterator that will iterate through the ions
  ION_ITERATOR_T* ion_iterator = new_ion_iterator(ion_series);
  
  // while there are ion's in ion iterator, add matched observed peak intensity
  while(ion_iterator_has_next(ion_iterator)){
    ion = ion_iterator_next(ion_iterator);
    intensity_array_idx = (int)(get_ion_mass_z(ion) / bin_width + 0.5);
    ion_type = get_ion_type(ion);

    // skip ions that are located beyond max mz limit
    if(intensity_array_idx >= scorer->sp_max_mz){
      continue;
    }

    // DEBUG
    /*
    if(ion_type == B_ION){
      if(ion_is_modified(ion)){
        carp(CARP_INFO, "idx: %d, adding ion type: MOD-%s",  intensity_array_idx,  "B");
      }
      else{
        carp(CARP_INFO, "idx: %d, adding ion type: %s",  intensity_array_idx,  "B");
      }
    }
    else if(ion_type == Y_ION){
      if(ion_is_modified(ion)){
        carp(CARP_INFO, "idx: %d, adding ion type: MOD-%s",  intensity_array_idx,  "Y");
      }
      else{
        carp(CARP_INFO, "idx: %d, adding ion type: %s",  intensity_array_idx,  "Y");
      }
    }
    else{
      carp(CARP_INFO, "idx: %d, adding ion type: %s",  intensity_array_idx,  "A");
    }
    */
    // is it B, Y ion?
    if(ion_type == B_ION || 
       ion_type == Y_ION){

      // neutral loss peak?
      if(ion_is_modified(ion)){
        // Add peaks of intensity of 10.0 for neutral loss of H2O, ammonia.
        // In addition, add peaks of intensity of 10.0 to +/- 1 m/z flanking each neutral loss.
        // add_intensity(theoretical, intensity_array_idx, 10);
        // add_intensity(theoretical, intensity_array_idx + 1, 10);
        // add_intensity(theoretical, intensity_array_idx - 1, 10);
      }
      else{
        // Add peaks of intensity 50.0 for B, Y type ions. 
        // In addition, add peaks of intensity of 25.0 to +/- 1 m/z flanking each B, Y ion.
        add_intensity(theoretical, intensity_array_idx, 50);

        // skip ions that are located beyond max mz limit
        if((intensity_array_idx + 1)< scorer->sp_max_mz){
          add_intensity(theoretical, intensity_array_idx + 1, 25);
        }
        
        add_intensity(theoretical, intensity_array_idx - 1, 25);

        // add neutral loss of water and NH3
        // mass_z + (modification_masses[(int)ion_modification]/(float)charge) * modification_count;  

        if(ion_type == B_ION){
          int h2o_array_idx = (int)((get_ion_mass_z(ion) - MASS_H2O_MONO /*charge*/) / bin_width + 0.5);
          add_intensity(theoretical, h2o_array_idx, 10);
        }

        int nh3_array_idx = (int)((get_ion_mass_z(ion) -  MASS_NH3_MONO/*charge*/) / bin_width + 0.5);
        add_intensity(theoretical, nh3_array_idx, 10);        
      }
      

    }// is it A ion?
    else if(ion_type == A_ION){
      // Add peaks of intensity 10.0 for A type ions. 
      // In addition, add peaks of intensity of 10.0 to +/- 1 m/z flanking each A type ion.
        add_intensity(theoretical, intensity_array_idx, 10);
      /*
      add_intensity(theoretical, intensity_array_idx + 1, 10);
      add_intensity(theoretical, intensity_array_idx - 1, 10);
      */
    }
    else{// ERROR!, only should create B, Y, A type ions for xcorr theoreical 
      carp(CARP_ERROR, "only should create B, Y, A type ions for xcorr theoretical spectrum");
      return FALSE;
    }
  }

    
  // free heap
  free_ion_iterator(ion_iterator);

  // DEBUG
  /*
  int i = 0;
  for(; i < scorer->sp_max_mz; i++){
    if(theoretical[i] != 0){
      carp(CARP_INFO, "Theoretical array[%d]: %.2f", i, theoretical[i]);
    }
  }
  */
  return TRUE;
}

/**
 * create the intensity arrays for both observed and theoretical spectrum
 * SCORER must have been created for XCORR type
 * \returns TRUE if successful, else FLASE
 */
BOOLEAN_T create_intensity_array_xcorr(
  SPECTRUM_T* spectrum,    ///< the spectrum to score(observed) -in
  SCORER_T* scorer,        ///< the scorer object -in/out
  int charge               ///< the peptide charge -in 
  )
{

  // DEBUG
  // carp(CARP_INFO, "precursor_mz: %.1f", precursor_mz);
  
  // if score type equals XCORR
  if(scorer->type != XCORR){
    carp(CARP_ERROR, "Incorrect scorer type, only use this method for XCORR scorers");
    return FALSE;
  }
    
  // create intensity array for observed spectrum 
  if(!create_intensity_array_observed(scorer, spectrum, charge)){
    carp(CARP_ERROR, "Failed to preprocess observed spectrum for Xcorr");
    return FALSE;
  }
  
  // scorer now been initialized!, ready to score peptides..
  scorer->initialized = TRUE;

  return TRUE;
}

/**
 * Uses an iterative cross correlation
 *
 *\return the final cross correlation score between the observed and the
 *theoretical spectra
 */
float cross_correlation(
  SCORER_T* scorer,  ///< the scorer object that contains observed spectrum -in
  float* theoretical ///< the theoretical spectrum to score against the observed spectrum -in
  )
{
  int size = (int)scorer->sp_max_mz;
  float score_at_zero = 0;
  float* observed = scorer->observed;
  
  // compare each location in theoretical spectrum
  int idx;
  for(idx = 0; idx < size; ++idx){
    score_at_zero += observed[idx] * theoretical[idx];
  }

  return score_at_zero / 10000.0;
}

/**
 * given a spectrum and ion series calculates the xcorr score
 *\returns the xcorr score 
 */
float gen_score_xcorr(
  SCORER_T* scorer,        ///< the scorer object -in
  SPECTRUM_T* spectrum,    ///< the spectrum to score -in
  ION_SERIES_T* ion_series ///< the ion series to score against the spectrum -in
  )
{
  float final_score = 0;
  float* theoretical = NULL;

  // initialize the scorer before scoring if necessary
  // preprocess the observed spectrum in scorer
  if(!scorer->initialized){
    // create intensity array for observed spectrum, if already not been done
    if(!create_intensity_array_xcorr(spectrum, scorer, get_ion_series_charge(ion_series))){
      carp(CARP_ERROR, "failed to produce XCORR");
      free(spectrum);
      free(ion_series);
      free(scorer);
      exit(1);
    }
  }
  
  // create theoretical array
  theoretical = (float*)mycalloc(scorer->sp_max_mz, sizeof(float));
  
  // create intensity array for theoretical spectrum 
  if(!create_intensity_array_theoretical(scorer, ion_series, theoretical)){
    carp(CARP_ERROR, "failed to create theoretical spectrum for Xcorr");
    return FALSE;
  }
  
  // do cross correlation between observed spectrum(in scorer) and theoretical spectrum.
  // use the two intensity arrays that were created
  final_score = cross_correlation(scorer, theoretical);

  // free theoretical spectrum
  free(theoretical);

  // debug
  // carp(CARP_INFO, "xcorr: %.2f", final_score);

  
  // return score
  return final_score;
}

/*************************************
 * Score for LOGP_EXP_SP && LOGP_BONF_EXP_SP
 *
 *
 *
 ************************************/

/**
 * Compute a p-value for a given score w.r.t. an exponential with the given parameters.
 *\returns the -log(p_value) of the exponential distribution
 */
float score_logp_exp_sp(
  float sp_score, ///< The sp score for the scoring peptide -in
  float mean      ///< The overall mean of the sp scored peptides -in
  )
{
  return -log( exp(-(1/mean) * sp_score) );
}

/**
 * Compute a p-value for a given score w.r.t. an exponential with the given parameters.
 *\returns the -log(p_value) of the exponential distribution with Bonferroni correction
 */
float score_logp_bonf_exp_sp(
  float sp_score, ///< The sp score for the scoring peptide -in
  float mean,      ///< The overall mean of the sp scored peptides -in
  int num_peptide  ///< The number of peptides scored for sp
  )
{
  double p_value = exp(-(1/mean) * sp_score);
  
  // The Bonferroni correction 
  // use original equation 1-(1-p_value)^n when p is not too small
  if(p_value > BONFERRONI_CUT_OFF_P || p_value*num_peptide > BONFERRONI_CUT_OFF_NP){
    return -log(1-pow((1-p_value), num_peptide));
  }
  // else, use the approximation
  else{
    return -log(p_value*num_peptide);
  }
}

/**
 * Compute a p-value for a given score w.r.t. a Weibull with given parameters.
 *\returns the -log(p_value)
 */
float score_logp_weibull(
  float score, ///< The score for the scoring peptide -in
  float eta,  ///< The eta parameter of the Weibull
  float beta ///< The beta parameter of the Weibull
  ){
  return pow(score/eta, beta);
}


/**
 * Compute a p-value for a given score w.r.t. a Weibull with given parameters.
 *\returns the -log(p_value)
 */
double score_logp_bonf_weibull(
  float score, ///< The score for the scoring peptide -in
  float eta,  ///< The eta parameter of the Weibull
  float beta, ///< The beta parameter of the Weibull
  float shift, ///< The shift parameter of the Weibull
  int num_peptide ///< The number of peptides
  ){
  carp(CARP_DETAILED_DEBUG, "Stat: score = %.6f", score);
  double p_value = exp( - pow( (score+shift)/eta, beta));
  carp(CARP_DETAILED_DEBUG, "Stat: pvalue before = %.15f", p_value);

  // The Bonferroni correction 
  // use original equation 1-(1-p_value)^n when p is not too small
  if(p_value > BONFERRONI_CUT_OFF_P 
     || p_value*num_peptide > BONFERRONI_CUT_OFF_NP){

    double corrected_pvalue = -log(1-pow((1-p_value), num_peptide));
    carp(CARP_DETAILED_DEBUG, "Stat: pvalue after = %.6f", corrected_pvalue);
    return corrected_pvalue;
  }
  // else, use the approximation
  else{
    double corrected_pvalue = -log(p_value*num_peptide);
    carp(CARP_DETAILED_DEBUG, "Stat: pvalue after = %.6f", corrected_pvalue);
    return corrected_pvalue;
  }

}

/**
 * Compute a p-value for a given score w.r.t. an EVD with the given parameters.
 * Function: ExtremeValueP()
 * 
 * Purpose:  Calculate P(S>x) according to an extreme
 *           value distribution, given x and the parameters
 *           of the distribution (characteristic
 *           value mu, decay constant lambda).
 *           
 *           This function is exquisitely prone to
 *           floating point exceptions if it isn't coded
 *           carefully.
 *           
 * Args:     x      = score
 *           mu     = characteristic value of extreme value distribution
 *           lambda = decay constant of extreme value distribution
 *           
 *\returns P(S>x)
 */
double compute_evd_pvalue(
  float score, ///< The xcorr score for the scoring peptide -in
  float evd_mu, ///<  EVD parameter Xcorr(characteristic value of extreme value distribution) -in
  float evd_lambda ///< EVD parameter Xcorr(decay constant of extreme value distribution) -in
  )
{
  // These constants are hardware dependent.
  // These values should be good for double precision floating point
  // numbers compatible with the IEEE 754 standard.
  // DBL_EPSILON = 2.2204460492503131e-16
  // DBL_MAX_10_EXP = 308
  
  double p_value = 0;
  
  // avoid exceptions near P=1.0
  if((evd_lambda * (score - evd_mu)) <= -1.0 * log(-1.0 * log(DBL_EPSILON))){
    return 1.0;
  }
  
  // avoid underflow fp exceptions near P=0.0
  if((evd_lambda * (score - evd_mu)) >= 2.3 * DBL_MAX_10_EXP){
    return 0.0;
  }
  
  // a roundoff issue arises; use 1 - e^-x --> x for small x
  p_value = exp(-1.0 * evd_lambda * (score - evd_mu));
  if (p_value < 1e-7){
    return p_value;
  }
  else{
    return (1.0 - exp(-1.0 * p_value));
  }
}

/**
 * Compute a p-value for a given score w.r.t. an EVD with the given parameters.
 *\returns the -log(p_value) of the EVD distribution 
 */
float score_logp_evd_xcorr(
  float xcorr_score, ///< The xcorr score for the scoring peptide -in
  float mu, ///<  EVD parameter Xcorr(characteristic value of extreme value distribution) -in
  float l_value ///< EVD parameter Xcorr(decay constant of extreme value distribution) -in
  )
{
  return -log(compute_evd_pvalue(xcorr_score, mu, l_value));
}

/**
 * Compute a p-value for a given score w.r.t. an EVD with the given parameters.
 *\returns the -log(p_value) of the EVD distribution with Bonferroni correction
 */
float score_logp_bonf_evd_xcorr(
  float xcorr_score, ///< The xcorr score for the scoring peptide -in
  float mu, ///<  EVD parameter Xcorr(characteristic value of extreme value distribution) -in
  float l_value, ///< EVD parameter Xcorr(decay constant of extreme value distribution) -in
  int num_peptide  ///< The number of peptides scored for sp -in
  )
{
  double p_value = compute_evd_pvalue(xcorr_score, mu, l_value);

  // DEBUG
  carp(CARP_DEBUG, "p_value: %E, experiment_size: %d", p_value, num_peptide);

  // The Bonferroni correction 
  // use original equation 1-(1-p_value)^n when p is not too small
  if(p_value > BONFERRONI_CUT_OFF_P || p_value*num_peptide > BONFERRONI_CUT_OFF_NP){
    return -log(1-pow((1-p_value), num_peptide));
  }
  // else, use the approximation
  else{
    return -log(p_value*num_peptide);
  }
}

/*****************************************************
 * General purpose functions
 * 
 *****************************************************/

/**
 * Score a spectrum vs. an ion series
 */
float score_spectrum_v_ion_series(
  SCORER_T* scorer,        ///< the scorer object -in
  SPECTRUM_T* spectrum,    ///< the spectrum to score -in
  ION_SERIES_T* ion_series ///< the ion series to score against the spectrum -in
  )
{
  float final_score = 0;

  // if score type equals SP
  if(scorer->type == SP){
    final_score = gen_score_sp(scorer, spectrum, ion_series);
  }
  else if(scorer->type == XCORR){
    final_score = gen_score_xcorr(scorer, spectrum, ion_series);
  }
  // FIXME, later add different score types...
  else{
    carp(CARP_ERROR, "no scoring method availiable for the scorers' score type");
  }
  
  return final_score;
}

/**
 * Score a spectrum vs. another spectrum
 */
float score_spectrum_v_spectrum(
  SCORER_T* scorer,           ///< the scorer object
  SPECTRUM_T* first_spectrum, ///< the first spectrum to score 
  SPECTRUM_T* second_spectrum ///<  the second spectrum to score
);

/**
 * Creates the an array of ion constraints for GMTK models.
 * 0  - b
 * 1  - b-nh3
 * 2  - b-h2o
 * 3  - b+2
 * 4  - b-nh3+2
 * 5  - b-h2o+2
 * 6  - y
 * 7  - y-nh3
 * 8  - y-h2o
 * 9  - y+2
 * 10 - y-nh3+2
 * 11 - y-h2o+2
 * 12 - a
 * 13 - a-nh3
 * 14 - a-h2o
 * 15 - a+2
 * 16 - a-nh3+2
 * 17 - a-h2o+2
 */
ION_CONSTRAINT_T** single_ion_constraints(
  void
){

  carp(CARP_INFO, "Num ion series %i", GMTK_NUM_ION_SERIES);
  ION_CONSTRAINT_T** ion_constraints = (ION_CONSTRAINT_T**) 
    malloc(GMTK_NUM_ION_SERIES * sizeof(ION_CONSTRAINT_T*));

  ION_TYPE_T ion_types[GMTK_NUM_BASE_IONS] = { B_ION, Y_ION, A_ION }; 
  int charges[GMTK_NUM_CHARGES] = { 1, 2 }; 

  //MASS_TYPE_T mass_type = MONO; // TODO maybe change to parameter file
  MASS_TYPE_T mass_type = get_mass_type_parameter("fragment-mass");

  int ion_constraint_idx = 0;

  int ion_type_idx;
  // b and y. NOTE keep in synch with GMTKmodel.py writeIonFilesC
  for (ion_type_idx=0; ion_type_idx < GMTK_NUM_BASE_IONS; ion_type_idx++){

    int charge_idx;
    for (charge_idx=0; charge_idx < GMTK_NUM_CHARGES; charge_idx++){

      int neutral_idx;
      for(neutral_idx=0; neutral_idx< GMTK_NUM_NEUTRAL_LOSS+1; neutral_idx++){
        ION_CONSTRAINT_T* ion_constraint =
          new_ion_constraint( mass_type, charges[charge_idx],
                              ion_types[ion_type_idx], FALSE);
        set_ion_constraint_exactness(ion_constraint, TRUE);
        if (neutral_idx == 0){
          ;
        }
        else if (neutral_idx == 1){
          set_ion_constraint_modification(ion_constraint, NH3, -1);
        } else if (neutral_idx == 2){
          set_ion_constraint_modification(ion_constraint, H2O, -1);
        }
        ion_constraints[ion_constraint_idx] = ion_constraint;
        ion_constraint_idx++;
      }
    }
  }
  return ion_constraints;
}

/**
 * Frees the single_ion_constraints array
 */
void free_single_ion_constraints(
    ION_CONSTRAINT_T** ion_constraints
    ){
  int constraint_idx;
  for (constraint_idx=0; constraint_idx<GMTK_NUM_ION_SERIES; constraint_idx++){
    free_ion_constraint(ion_constraints[constraint_idx]);
  }
  free(ion_constraints);
}

/**
 * Creates the an array of ion constraints for GMTK models.

 * TODO do we need one for paired and single? Do we want an iterator?
 */
ION_CONSTRAINT_T** paired_ion_constraints(
  void
  ){

  carp(CARP_INFO, "Num ion series %i", GMTK_NUM_PAIRED_ION_SERIES);
  ION_CONSTRAINT_T** base_ion_constraints = single_ion_constraints(); 
  ION_CONSTRAINT_T** ion_constraints = (ION_CONSTRAINT_T**) 
    malloc(2 * GMTK_NUM_PAIRED_ION_SERIES * sizeof(ION_CONSTRAINT_T*));

  // FIX magic numbers
  /* 0  - b
   * 1  - b-nh3
   * 2  - b-h2o
   * 3  - b+2
   * 4  - b-nh3+2
   * 5  - b-h2o+2
   * 6  - y
   * 7  - y-nh3
   * 8  - y-h2o
   * 9  - y+2
   * 10 - y-nh3+2
   * 11 - y-h2o+2
   * 12 - a
   * 13 - a-nh3
   * 14 - a-h2o
   * 15 - a+2
   * 16 - a-nh3+2
   * 17 - a-h2o+2*/
 
  int indices[GMTK_NUM_PAIRED_ION_SERIES * 2] = { 
    0, 6, // b,y
    0, 12,// b,a
    6, 12,// y,a
    0, 2, // b,b-h2o
    0, 1, // b,b-nh3
    0, 3, // b,b+2
    2, 5, // b-h2o,b-h2o+2
    1, 4, // b-nh3,b-nh3+2
    6, 8, // y,y-h2o
    6, 7, // y,y-nh3
    6, 9, // y,y+2
    8, 11,// y-h2o,y-h2o+2
    7, 10,// y-nh3,y-nh3+2
    12,15,// a,a+2
    3,9   // b+2,y+2
  };

  int idx;
  for (idx = 0; idx < GMTK_NUM_PAIRED_ION_SERIES * 2; idx++){
    ion_constraints[idx] = copy_ion_constraint_ptr(
        base_ion_constraints[indices[idx]]);
  }
  return ion_constraints;
}

/**
 * Frees the paired ion_constraints array
 */
void free_paired_ion_constraints(
    ION_CONSTRAINT_T** ion_constraints
    ){
  int constraint_idx;
  for (constraint_idx=0; constraint_idx<GMTK_NUM_PAIRED_ION_SERIES; 
       constraint_idx++){
    free_ion_constraint(ion_constraints[constraint_idx]);
  }
  free(ion_constraints);
}

/**
 * Create ion files (for paired-ion GMTK model) 
 * in the output directory. 
 * \returns TRUE for success 
 */
BOOLEAN_T output_psm_files_paired(
  char* output_directory, ///< name of directory to place the ion files -in
  SPECTRUM_T* spectrum,     ///< input spectrum -in
  char** peptides, ///< peptide sequences -in 
  int num_peptides, ///< number of peptide sequences -in
  int charge, ///< the peptide charge -in
  int starting_sentence_idx ///< used to append to existing pfile -in
){
    
  char* peptide_sequence = NULL;
  carp(CARP_INFO, "Creating and outputting paired ions");

  // create the output directory, if not already
  int dir_access = S_IRWXU + S_IRWXG + S_IRWXO;
  if(access(output_directory, F_OK)){
    if (mkdir(output_directory, dir_access) != 0){
      carp(CARP_FATAL, "Trouble creating dir %s!", output_directory); 
      exit(1);
    }
  }

  // create and open (for appending) pfiles for each ion constraint
  carp(CARP_INFO, "Creating output file handles");
  FILE* ion_series_files[GMTK_NUM_ION_SERIES];
  int ion_series_idx;
  for ( ion_series_idx=0;ion_series_idx<GMTK_NUM_PAIRED_ION_SERIES;
        ion_series_idx++){
    char full_path[FILENAME_LENGTH];
    sprintf(full_path, "%s/%i-paired.prepfile", output_directory, ion_series_idx);
    if (open_file(full_path, "a", FALSE, "append", "", 
          &ion_series_files[ion_series_idx])==FALSE){
      carp(CARP_FATAL, "Trouble opening output file %s!", full_path); 
      exit(1);
    }
  }

  // iterate through each peptide
  carp(CARP_INFO, "Iterating through each peptide.");
  ION_SERIES_T* ion_series;
  ION_CONSTRAINT_T* ion_constraint = new_ion_constraint_gmtk(charge); 
  ION_CONSTRAINT_T** ion_constraints = paired_ion_constraints();

  int peptide_idx;
  for(peptide_idx=0; peptide_idx < num_peptides; peptide_idx++){ 
    if ((peptide_idx + 1)% 100 == 0){
      carp(CARP_INFO, "At peptide %i of %i", peptide_idx + 1, num_peptides);
    }
    peptide_sequence = peptides[peptide_idx];
    carp(CARP_DETAILED_DEBUG, "%s", peptide_sequence);

    // check peptide sequence
    if(!valid_peptide_sequence(peptide_sequence)){
      carp(CARP_FATAL, "not a valid peptide sequence: %s", peptide_sequence);
    }

    // create new ion series
    ion_series = new_ion_series(peptide_sequence, charge, ion_constraint);

    // now predict ions and assign them to their closest peaks
    predict_ions(ion_series);

    ion_series_assign_nearest_peaks(ion_series, spectrum);

    // create our ion constraints

    // FIX
    int constraint_idx;
    int ion_series_file_idx = 0;
    for (constraint_idx=0;constraint_idx<GMTK_NUM_PAIRED_ION_SERIES*2;
         constraint_idx+=2){

      print_ion_series_paired_gmtk(
        ion_series,
        ion_constraints[constraint_idx],
        ion_constraints[constraint_idx+1],
        ion_series_files[ion_series_file_idx++],
        peptide_idx + starting_sentence_idx);
    }
    free_ion_series(ion_series);
  } 

  free_single_ion_constraints(ion_constraints);
  free_ion_constraint(ion_constraint);
  return TRUE;
}

/**
 * Create ion files (for GMTK) in the output directory. 
 * \returns TRUE for success 
 */
// PAIRED write an output_psm_files_paired
BOOLEAN_T output_psm_files_single(
  char* output_directory, ///< name of directory to place the ion files -in
  SPECTRUM_T* spectrum,     ///< input spectrum -in
  char** peptides, ///< peptide sequences -in 
  int num_peptides, ///< number of peptide sequences -in
  int charge, ///< the peptide charge -in
  int starting_sentence_idx ///< used to append to existing pfile -in
){
    
  char* peptide_sequence = NULL;
  carp(CARP_INFO, "Creating and outputting ions");

  // create the output directory, if not already
  int dir_access = S_IRWXU + S_IRWXG + S_IRWXO;
  if(access(output_directory, F_OK)){
    if (mkdir(output_directory, dir_access) != 0){
      carp(CARP_FATAL, "Trouble creating dir %s!", output_directory); 
      exit(1);
    }
  }

  // create and open (for appending) pfiles for each ion constraint
  carp(CARP_INFO, "Creating output file handles");
  FILE* ion_series_files[GMTK_NUM_ION_SERIES];
  int ion_series_idx;
  for (ion_series_idx=0;ion_series_idx<GMTK_NUM_ION_SERIES;ion_series_idx++){
    char full_path[FILENAME_LENGTH];
    sprintf(full_path, "%s/%i-single.prepfile", output_directory, ion_series_idx);
    if (open_file(full_path, "a", FALSE, "append", "", 
          &ion_series_files[ion_series_idx])==FALSE){
      carp(CARP_FATAL, "Trouble opening output file %s!", full_path); 
      exit(1);
    }
  }

  // iterate through each peptide
  carp(CARP_INFO, "Iterating through each peptide.");
  ION_SERIES_T* ion_series;
  ION_CONSTRAINT_T* ion_constraint = new_ion_constraint_gmtk(charge); 
  ION_CONSTRAINT_T** ion_constraints = single_ion_constraints();

  int peptide_idx;
  for(peptide_idx=0; peptide_idx < num_peptides; peptide_idx++){ 
    if ((peptide_idx + 1)% 100 == 0){
      carp(CARP_INFO, "At peptide %i of %i", peptide_idx + 1, num_peptides);
    }
    peptide_sequence = peptides[peptide_idx];
    carp(CARP_DETAILED_DEBUG, "%s", peptide_sequence);

    // check peptide sequence
    if(!valid_peptide_sequence(peptide_sequence)){
      carp(CARP_FATAL, "not a valid peptide sequence: %s", peptide_sequence);
    }

    // create new ion series
    ion_series = new_ion_series(peptide_sequence, charge, ion_constraint);

    // now predict ions and assign them to their closest peaks
    predict_ions(ion_series);

    ion_series_assign_nearest_peaks(ion_series, spectrum);

    // create our ion constraints

    // iterate through each ion_constraint
    int constraint_idx;
    for (constraint_idx=0;constraint_idx<GMTK_NUM_ION_SERIES;
         constraint_idx++){
      // output the ions that obey this constraint
      print_ion_series_single_gmtk(ion_series, 
          ion_constraints[constraint_idx], 
          ion_series_files[constraint_idx],
          peptide_idx + starting_sentence_idx);
    }
    carp(CARP_INFO, "Appended to ion files for: %s", peptide_sequence);

    free_ion_series(ion_series);
  } 

  free_single_ion_constraints(ion_constraints);
  free_ion_constraint(ion_constraint);
  return TRUE;
}

/*******************************
 * get, set methods for scorer
 *******************************/

/**
 *\returns the score type of the scorer
 */
SCORER_TYPE_T get_scorer_type(
  SCORER_T* scorer ///< the scorer object -in
  )
{
  return scorer->type;
}

/**
 *sets the scorer type
 */
void set_scorer_type(
  SCORER_T* scorer, ///< the scorer object -out                     
  SCORER_TYPE_T type ///< The type of scorer -in
)
{
  scorer->type = type;
}

/**
 *\returns the beta value of the scorer
 */
float get_scorer_sp_beta(
  SCORER_T* scorer ///< the scorer object -in
  )
{
  return scorer->sp_beta;
}

/**
 *sets the scorer beta value
 */
void set_scorer_sp_beta(
  SCORER_T* scorer, ///< the scorer object -out                     
  float sp_beta ///< used for Sp: the beta variable -in
  )
{
  scorer->sp_beta = sp_beta;
}

/**
 *\returns the max_mz value of the scorer
 */
float get_scorer_sp_max_mz(
  SCORER_T* scorer ///< the scorer object -in
  )
{
  return scorer->sp_max_mz;
}

/**
 *set the scorer max_mz value
 */
void set_scorer_sp_max_mz(
  SCORER_T* scorer, ///< the scorer object -out                     
  float sp_max_mz ///< used for Sp: the max_mz variable -in
  )
{
  scorer->sp_max_mz = sp_max_mz;
}

/**
 * adds the intensity at add_idx
 * if, there already exist a peak at the index, only overwrite if
 * intensity is larger than the existing peak.
 */
void add_intensity(
  float* intensity_array, ///< the intensity array to add intensity at index add_idx -out
  int add_idx,            ///< the idex to add the intensity -in
  float intensity         ///< the intensity to add -in
  )
{
  if(intensity_array[add_idx] < intensity){
    intensity_array[add_idx] = intensity;
  }
}

/**
 *\returns the fraction of b,y ions matched for scoring SP, the values is valid for the last ion series scored with this scorer object
 */
float get_scorer_sp_b_y_ion_fraction_matched(
  SCORER_T* scorer ///< the scorer object -out
  )
{
  return scorer->sp_b_y_ion_fraction_matched;
}

/**
 *\returns the number of b,y ions matched for scoring SP
 */
int get_scorer_sp_b_y_ion_matched(
  SCORER_T* scorer ///< the scorer object -out
  )
{
  return scorer->sp_b_y_ion_matched;
}

/**
 *\returns the number of b,y ions possible to match for scoring SP
 */
int get_scorer_sp_b_y_ion_possible(
  SCORER_T* scorer ///< the scorer object -out
  )
{
  return scorer->sp_b_y_ion_possible;
}

/**
 *\returns the array_resolution value of the scorer
 */
/*
float get_scorer_sp_array_resolution(
  SCORER_T* scorer ///< the scorer object -in
  )
{
  return scorer->sp_array_resolution;
}
*/

/**
 *set the scorer array_resolution value
 */
/*
void set_scorer_sp_array_resolution(
  SCORER_T* scorer, ///< the scorer object -out                     
  float sp_array_resolution ///< used for Sp: the array_resolution variable -in
  )
{
  scorer->sp_array_resolution = sp_array_resolution;
}
*/

/**
 *\returns the sum_resolution value of the scorer
 */
/*
float get_scorer_sp_sum_resolution(
  SCORER_T* scorer ///< the scorer object -in
  )
{
  return scorer->sp_sum_resolution;
}
*/

/**
 *set the scorer sum_resolution value
 */
/*
void set_scorer_sp_sum_resolution(
  SCORER_T* scorer, ///< the scorer object -out                     
  float sp_sum_resolution ///< used for Sp: the sum_resolution variable -in
  )
{
  scorer->sp_sum_resolution = sp_sum_resolution;
}
*/

/**
 *\returns the equalize_resolution value of the scorer
 */
/*
float get_scorer_sp_equalize_resolution(
  SCORER_T* scorer ///< the scorer object -in
  )
{
  return scorer->sp_equalize_resolution;
}
*/

/**
 *set the scorer equalize_resolution value
 */
/*
void set_scorer_sp_equalize_resolution(
  SCORER_T* scorer, ///< the scorer object -out                     
  float sp_equalize_resolution ///< used for Sp: the equalize_resolution variable -in
  )
{
  scorer->sp_equalize_resolution = sp_equalize_resolution;
}
*/

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 2
 * End:
 */
