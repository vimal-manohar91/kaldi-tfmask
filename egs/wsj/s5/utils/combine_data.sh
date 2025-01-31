#!/bin/bash
# Copyright 2012  Johns Hopkins University (Author: Daniel Povey).  Apache 2.0.
#           2014  David Snyder

# This script operates on a data directory, such as in data/train/.
# See http://kaldi.sourceforge.net/data_prep.html#data_prep_data
# for what these directories contain.

# Begin configuration section. 
extra_files= # specify addtional files in 'src-data-dir' to merge, ex. "file1 file2 ..."
skip_fix=false # skip the fix_data_dir.sh in the end
# End configuration section.

echo "$0 $@"  # Print the command line for logging

if [ -f path.sh ]; then . ./path.sh; fi
. parse_options.sh || exit 1;

if [ $# -lt 2 ]; then
  echo "Usage: combine_data.sh [--extra-files 'file1 file2'] <dest-data-dir> <src-data-dir1> <src-data-dir2> ..."
  echo "Note, files that don't appear in first source dir will not be added even if they appear in later ones."
  exit 1
fi

dest=$1;
shift;

first_src=$1;

rm -r $dest 2>/dev/null
mkdir -p $dest;

export LC_ALL=C

for dir in $*; do
  if [ ! -f $dir/utt2spk ]; then
    echo "$0: no such file $dir/utt2spk"
    exit 1;
  fi
done
    
for file in utt2spk utt2lang feats.scp text cmvn.scp segments reco2file_and_channel wav.scp spk2gender $extra_files; do
  if [ -f $first_src/$file ]; then
    for f in $*; do 
      cat $f/$file
    done | sort -k 1 > $dest/$file || exit 1;
    echo "$0: combined $file"
  else
    echo "$0 [info]: not combining $file as it does not exist"
  fi
done

utils/utt2spk_to_spk2utt.pl <$dest/utt2spk >$dest/spk2utt

if ! $skip_fix ; then
  bash -x utils/fix_data_dir.sh $dest || exit 1;
fi

exit 0
