#ifndef QRANKER_H_
#define QRANKER_H_
#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>
#include <cstdlib>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <math.h>
using namespace std;
#ifdef CRUX
#include "CruxApplication.h"
#include "carp.h"
#include "crux-utils.h"
#include "parameter.h"
#endif
#include "NeuralNet.h"
#include "DataSet.h"
#include "PSMScores.h"
#include "SQTParser.h"
#include "TabDelimParser.h"
#include "CruxApplication.h"

class QRanker: public CruxApplication
{

public:
	QRanker();
	virtual ~QRanker();

    int run();

    void train_net_ranking(PSMScores &set, int interval);
    void train_net_hinge(PSMScores &set, int interval);
    void train_many_general_nets();
    void train_many_target_nets();
    void train_many_nets();
    
    int getOverFDR(PSMScores &set, NeuralNet &n, double fdr);
    void getMultiFDR(PSMScores &set, NeuralNet &n, vector<double> &qval);
    void printNetResults(vector<int> &scores);
    void write_results(string filename, NeuralNet &net);
    void write_max_nets(string filename, NeuralNet *max_net);
    void write_unique_peptides(string filename, NeuralNet* max_net);
    void write_num_psm_per_spectrum(NeuralNet* max_net);

    inline void set_input_dir(string input_dir) {in_dir = input_dir; d.set_input_dir(input_dir);}
    inline void set_output_dir(string output_dir){out_dir = output_dir;}
    int set_command_line_options(int argc, char **argv);

    virtual int main(int argc, char** argv);
    virtual std::string getName();
    virtual std::string getDescription();

protected:

    Dataset d;
    string res_prefix;

    PSMScores fullset; 
    PSMScores trainset,testset,thresholdset;

    int seed;
    double selectionfdr;
    
    int num_features;
    NeuralNet net;
    int num_hu;
    double mu;
    double weightDecay;

    int ind_low;
    int interval;
    int niter;
    int switch_iter;
    double loss;

    int num_qvals;
    vector<double> qvals;
    vector<double> qvals1;
    vector<double> qvals2;
    
    vector<int> overFDRmulti;
    vector<int> max_overFDR;
    vector<int> ave_overFDR;
    NeuralNet* max_net_gen;
    NeuralNet* max_net_targ;
    NeuralNet* nets;

    string in_dir;
    string out_dir;
    int skip_cleanup_flag;
    int overwrite_flag;
    string fileroot;
    
    TabDelimParser pars;
    SQTParser sqtp;

};



#endif /*QRANKER_H_*/
