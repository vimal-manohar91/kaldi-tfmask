// nnet2bin/nnet2-train-simple.cc

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
#include "nnet2/train-nnet.h"
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
        "are read via a pipe from nnet-randomize-frames.  This version of the\n"
        "training program does not update the learning rate, but uses\n"
        "the learning rates stored in the neural nets.\n"
        "By default reads/writes model file (.mdl) but with --raw=true,\n"
        "reads/writes raw-nnet.\n"
        "\n"
        "Usage:  nnet2-train-simple [options] <model-in> <training-examples-in> <model-out>\n"
        "\n"
        "e.g.:\n"
        "nnet2-randomize-frames [args] | nnet2-train-simple 1.nnet ark:- 2.nnet\n";
    
    bool binary_write = true;
    bool zero_stats = true;
    int32 srand_seed = 0;
    std::string use_gpu = "yes";
    bool raw = false;    
    NnetSimpleTrainerConfig train_config;
    
    ParseOptions po(usage);
    po.Register("raw", &raw,
                "If true, read/write raw neural net rather than .mdl");
    po.Register("binary", &binary_write, "Write output in binary mode");
    po.Register("zero-stats", &zero_stats, "If true, zero occupation "
                "counts stored with the neural net (only affects mixing up).");
    po.Register("srand", &srand_seed, "Seed for random number generator "
                "(relevant if you have layers of type AffineComponentPreconditioned "
                "with l2-penalty != 0.0");
    po.Register("use-gpu", &use_gpu, "yes|no|optional, only has effect if compiled with CUDA"); 
    
    train_config.Register(&po);
    
    po.Read(argc, argv);
    
    if (po.NumArgs() != 3) {
      po.PrintUsage();
      exit(1);
    }
    srand(srand_seed);
    
#if HAVE_CUDA==1
    CuDevice::Instantiate().SelectGpuId(use_gpu);
#endif

    std::string nnet_rxfilename = po.GetArg(1),
        examples_rspecifier = po.GetArg(2),
        nnet_wxfilename = po.GetArg(3);

    int64 num_examples = 0;

    {
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
      
      if (zero_stats) {
        if (!raw) am_nnet.GetNnet().ZeroStats();
        else nnet.ZeroStats();
      }

      Nnet &nnet_ref = (raw ? nnet : am_nnet.GetNnet());
    
      { // want to make sure this object deinitializes before
        // we write the model, as it does something in the destructor.
        NnetSimpleTrainer trainer(train_config,
                                  &nnet_ref);
        
        SequentialNnetExampleReader example_reader(examples_rspecifier);

        for (; !example_reader.Done(); example_reader.Next(), num_examples++)
          trainer.TrainOnExample(example_reader.Value());  // It all happens here!
      }
    
      if (!raw) {
        Output ko(nnet_wxfilename, binary_write);
        trans_model.Write(ko.Stream(), binary_write);
        am_nnet.Write(ko.Stream(), binary_write);
      } else {
        WriteKaldiObject(nnet, nnet_wxfilename, binary_write);
      }
    }
#if HAVE_CUDA==1
    CuDevice::Instantiate().PrintProfile();
#endif
    
    KALDI_LOG << "Finished training, processed " << num_examples
              << " training examples.  Wrote model to "
              << nnet_wxfilename;
    return (num_examples == 0 ? 1 : 0);
  } catch(const std::exception &e) {
    std::cerr << e.what() << '\n';
    return -1;
  }
}


