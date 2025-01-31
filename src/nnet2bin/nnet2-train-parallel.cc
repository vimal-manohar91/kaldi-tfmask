// nnet2bin/nnet2-train-parallel.cc

// Copyright 2012  Johns Hopkins University (author: Daniel Povey)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "hmm/transition-model.h"
#include "nnet2/nnet-randomize.h"
#include "nnet2/nnet-update-parallel.h"
#include "nnet2/nnet-update.h"
#include "nnet2/am-nnet.h"


int main(int argc, char *argv[]) {
  try {
    using namespace kaldi;
    using namespace kaldi::nnet2;
    typedef kaldi::int32 int32;
    typedef kaldi::int64 int64;

    const char *usage =
        "Train the neural network parameters with backprop and stochastic\n"
        "gradient descent using minibatches.  The training frames and labels\n"
        "are read via a pipe from nnet-randomize-frames.  This is like nnet2-train-simple,\n"
        "but uses multiple threads in a Hogwild type of update.\n"
        "By default reads/writes model file (.mdl) but with --raw=true,\n"
        "reads/writes raw-nnet.\n"
        "\n"
        "Usage:  nnet2-train-parallel [options] <model-in> <training-examples-in> <model-out>\n"
        "or:  nnet2-train-parallel --raw [options] <raw-nnet-in> <training-examples-in> <raw-nnet-out>\n"
        "\n"
        "e.g.:\n"
        "nnet2-randomize-frames [args] | nnet2-train-parallel 1.mdl ark:- 2.nnet\n";
    
    bool binary_write = true;
    bool zero_stats = true;
    int32 minibatch_size = 1024;
    int32 srand_seed = 0;
    bool raw = false;
    NnetUpdaterConfig updater_config;
    
    ParseOptions po(usage);

    po.Register("raw", &raw,
                "If true, read/write raw neural net rather than .mdl");
    po.Register("binary", &binary_write, "Write output in binary mode");
    po.Register("zero-stats", &zero_stats, "If true, zero stats "
                "stored with the neural net (only affects mixing up).");
    po.Register("srand", &srand_seed,
                "Seed for random number generator (e.g., for dropout)");
    po.Register("num-threads", &g_num_threads, "Number of training threads to use "
                "in the parallel update. [Note: if you use a parallel "
                "implementation of BLAS, the actual number of threads may be larger.]");
    po.Register("minibatch-size", &minibatch_size, "Number of examples to use for "
                "each minibatch during training.");
    updater_config.Register(&po);

    po.Read(argc, argv);
    srand(srand_seed);

    if (po.NumArgs() != 3) {
      po.PrintUsage();
      exit(1);
    }
    
    std::string nnet_rxfilename = po.GetArg(1),
        examples_rspecifier = po.GetArg(2),
        nnet_wxfilename = po.GetArg(3);

    TransitionModel trans_model;
    AmNnet am_nnet;
    Nnet nnet; // used if raw==true.

    if (!raw) {
      bool binary_read;
      Input ki(nnet_rxfilename, &binary_read);
      trans_model.Read(ki.Stream(), binary_read);
      am_nnet.Read(ki.Stream(), binary_read);
    } else {
      ReadKaldiObject(nnet_rxfilename, &nnet);
    }
      

    KALDI_ASSERT(minibatch_size > 0);

    if (zero_stats) {
      if (!raw) am_nnet.GetNnet().ZeroStats();
      else nnet.ZeroStats();
    }
    
    double num_examples = 0;
    SequentialNnetExampleReader example_reader(examples_rspecifier);


    Nnet &nnet_ref = (raw ? nnet : am_nnet.GetNnet());

    DoBackpropParallel(nnet_ref,
                       minibatch_size,
                       &example_reader,
                       updater_config,
                       &num_examples,
                       &nnet_ref);
    
    if (!raw) {
      Output ko(nnet_wxfilename, binary_write);
      trans_model.Write(ko.Stream(), binary_write);
      am_nnet.Write(ko.Stream(), binary_write);
    } else {
      WriteKaldiObject(nnet, nnet_wxfilename, binary_write);
    }
    
    KALDI_LOG << "Finished training, processed " << num_examples
              << " training examples (weighted).  Wrote model to "
              << nnet_wxfilename;
    return (num_examples == 0 ? 1 : 0);
  } catch(const std::exception &e) {
    std::cerr << e.what() << '\n';
    return -1;
  }
}


