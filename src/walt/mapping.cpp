#include "mapping.hpp"

#include "smithlab_os.hpp"
#include "OptionParser.hpp"

void LoadReadsFromFastqFile(FILE * fin, const uint32_t read_start_idx,
                            const uint32_t n_reads_to_process,
                            uint32_t& num_of_reads, vector<string>& read_names,
                            vector<string>& read_seqs,
                            vector<string>& read_scores) {
  char cline[MAX_LINE_LENGTH];
  string line;
  int line_code = 0;
  uint32_t line_count = 0;
  num_of_reads = 0;
  uint32_t lim = n_reads_to_process * 4;
  while (line_count < lim && fgets(cline, MAX_LINE_LENGTH, fin)) {
    cline[strlen(cline) - 1] = 0;
    line = cline;
    switch (line_code) {
      case 0: {
        size_t space_pos = line.find_first_of(' ');
        if (space_pos == string::npos) {
          read_names[num_of_reads] = line.substr(1);
        } else {
          read_names[num_of_reads] = line.substr(1, space_pos - 1);
        }
        break;
      }
      case 1: {
        read_seqs[num_of_reads] = line;
        break;
      }
      case 2: {
        break;
      }
      case 3: {
        read_scores[num_of_reads] = line;
        num_of_reads++;
        break;
      }
    }
    ++line_count;
    ++line_code;
    if (line_code == 4) {
      line_code = 0;
    }
  }
}

void C2T(const string& org_read, const uint32_t& read_len, string& read) {
  for (uint32_t i = 0; i < read_len; ++i) {
    if ('N' == org_read[i]) {
      read += 'T';
    } else if ('C' == org_read[i]) {
      read += 'T';
    } else {
      read += org_read[i];
    }
  }
}

void G2A(const string& org_read, const uint32_t& read_len, string& read) {
  for (uint32_t i = 0; i < read_len; ++i) {
    if ('N' == org_read[i]) {
      read += 'A';
    } else if ('G' == org_read[i]) {
      read += 'A';
    } else {
      read += org_read[i];
    }
  }
}

uint32_t LowerBound(uint32_t low, uint32_t high, const char& chr,
                    const uint32_t& cmp_pos, const Genome& genome,
                    const HashTable& hash_table) {
  uint32_t mid = 0;
  while (low < high) {
    mid = low + (high - low) / 2;
    char c = genome.sequence[hash_table.index[mid] + cmp_pos];
    if (c >= chr) {
      high = mid;
    } else {
      low = mid + 1;
    }
  }
  return low;
}

uint32_t UpperBound(uint32_t low, uint32_t high, const char& chr,
                    const uint32_t& cmp_pos, const Genome& genome,
                    const HashTable& hash_table) {
  uint32_t mid = 0;
  while (low < high) {
    mid = low + (high - low + 1) / 2;
    char c = genome.sequence[hash_table.index[mid] + cmp_pos];
    if (c <= chr) {
      low = mid;
    } else {
      high = mid - 1;
    }
  }
  return low;
}

void IndexRegion(const string& read, const Genome& genome,
                 const HashTable& hash_table, const uint32_t& seed_len,
                 pair<uint32_t, uint32_t>& region) {
  uint32_t l = region.first, u = region.second - 1;
  for (uint32_t p = F2SEEDWIGTH; p < seed_len; ++p) {
    uint32_t care_pos = F2SEEDPOSITION[p];
    l = LowerBound(l, u, read[care_pos], care_pos, genome, hash_table);
    u = UpperBound(l, u, read[care_pos], care_pos, genome, hash_table);
  }

  if (l > u) {
    region.first = 1;
    region.second = 0;
    return;
  }

  region.first = l;
  region.second = u;
}

void SingleEndMapping(const string& org_read, const Genome& genome,
                      const HashTable& hash_table, const char& strand,
                      const bool& AG_WILDCARD, BestMatch& best_match) {
  uint32_t read_len = org_read.size();
  uint32_t seed_len = getSeedLength(read_len);

  string read;
  if (AG_WILDCARD) {
    G2A(org_read, read_len, read);
  } else {
    C2T(org_read, read_len, read);
  }

  for (uint32_t seed_i = 0; seed_i < SEEPATTERNLEN; ++seed_i) {
    if (best_match.mismatch == 0 && seed_i) /* different with paired-end*/
      break;
    string read_seed = read.substr(seed_i);
    uint32_t hash_value = getHashValue(read_seed.c_str());
    pair<uint32_t, uint32_t> region;
    region.first = hash_table.counter[hash_value];
    region.second = hash_table.counter[hash_value + 1];

    if (region.first == region.second)
      continue;

    IndexRegion(read_seed, genome, hash_table, seed_len, region);
    if(region.second - region.first + 1 > 50000){
      continue;
    }
    for (uint32_t j = region.first; j <= region.second; ++j) {
      uint32_t genome_pos = hash_table.index[j];
      uint32_t chr_id = getChromID(genome.start_index, genome_pos);
      if (genome_pos - genome.start_index[chr_id] < seed_i)
        continue;
      genome_pos = genome_pos - seed_i;
      if (genome_pos + read_len >= genome.start_index[chr_id + 1])
        continue;

      /* check the position */
      uint32_t num_of_mismatch = 0;
      for (uint32_t q = genome_pos, p = 0;
          p < read_len && num_of_mismatch <= best_match.mismatch; ++q, ++p) {
        if (genome.sequence[q] != read[p]) {
          num_of_mismatch++;
        }
      }

      if (num_of_mismatch < best_match.mismatch) {
        best_match = BestMatch(genome_pos, 1, strand, num_of_mismatch);
      } else if (best_match.mismatch == num_of_mismatch
          && best_match.genome_pos != genome_pos) {
        best_match.genome_pos = genome_pos;
        best_match.strand = strand;
        best_match.times++;
      }
    }
  }
}

void OutputUniquelyAndAmbiguousMapped(FILE * fout, const BestMatch best_match,
                                      const string& read_name,
                                      const string& read_seq,
                                      const string& read_score,
                                      const Genome& genome) {
  uint32_t chr_id = getChromID(genome.start_index, best_match.genome_pos);
  uint32_t start_pos = best_match.genome_pos - genome.start_index[chr_id];
  if ('-' == best_match.strand) {
    start_pos = genome.length[chr_id] - start_pos - read_seq.size();
  }
  uint32_t end_pos = start_pos + read_seq.size();

  fprintf(fout, "%s\t%u\t%u\t%s\t%u\t%c\t%s\t%s\n", genome.name[chr_id].c_str(),
          start_pos, end_pos, read_name.c_str(), best_match.mismatch,
          best_match.strand, read_seq.c_str(), read_score.c_str());
}

void OutputUnmapped(FILE * fout, const string& read_name,
                    const string& read_seq, const string& read_score) {
  fprintf(fout, "%s\t%s\t%s\n", read_name.c_str(), read_seq.c_str(),
          read_score.c_str());
}

void ProcessSingledEndReads(const string& index_file,
                            const string& reads_file_s,
                            const string& output_file,
                            const uint32_t& n_reads_to_process,
                            const uint32_t& max_mismatches,
                            const bool& AG_WILDCARD, const bool& ambiguous,
                            const bool& unmapped) {
  // LOAD THE INDEX HEAD INFO
  Genome genome;
  HashTable hash_table;

  uint32_t size_of_index;
  ReadIndexHeadInfo(index_file, genome, size_of_index);
  genome.sequence.resize(genome.length_of_genome);
  hash_table.counter.resize(power(4, F2SEEDWIGTH) + 1);
  hash_table.index.resize(size_of_index);

  vector<string> index_names;
  if (!AG_WILDCARD) {
    index_names.push_back(index_file + "_CT00");
    index_names.push_back(index_file + "_CT01");
  } else {
    index_names.push_back(index_file + "_GA10");
    index_names.push_back(index_file + "_GA11");
  }

  vector<string> read_names(n_reads_to_process);
  vector<string> read_seqs(n_reads_to_process);
  vector<string> read_scores(n_reads_to_process);

  vector<BestMatch> map_results(n_reads_to_process);
  FILE * fin = fopen(reads_file_s.c_str(), "r");
  if (!fin) {
    throw SMITHLABException("cannot open input file " + reads_file_s);
  }

  clock_t start_t = clock();
  FILE * fout = fopen(output_file.c_str(), "w");
  FILE * fambiguous = NULL, *funmapped = NULL;
  if (ambiguous) {
    fambiguous = fopen(string(output_file + "_ambiguous").c_str(), "w");
  }
  if (unmapped) {
    funmapped = fopen(string(output_file + "_unmapped").c_str(), "w");
  }

  uint32_t num_of_reads;
  uint32_t num_of_total_reads = 0;
  uint32_t num_of_unique_mapped = 0;
  uint32_t num_of_ambiguous_mapped = 0;
  uint32_t num_of_unmapped = 0;
  fprintf(stderr, "[MAPPING READS FROM %s]\n", reads_file_s.c_str());
  fprintf(stderr, "[MAPPING READS 0");
  for (uint32_t i = 0;; i += n_reads_to_process) {
    LoadReadsFromFastqFile(fin, i, n_reads_to_process, num_of_reads, read_names,
                           read_seqs, read_scores);
    if (num_of_reads == 0)
      break;

    // Initialize the results
    BestMatch best_match(0, 0, '+', max_mismatches);
    for (uint32_t j = 0; j < num_of_reads; ++j) {
      map_results[j] = best_match;
    }
    num_of_total_reads += num_of_reads;
    fprintf(stderr, "...%u", num_of_total_reads);
    for (uint32_t fi = 0; fi < 2; ++fi) {
      ReadIndex(index_names[fi], genome, hash_table);
      for (uint32_t j = 0; j < num_of_reads; ++j) {
        char strand = fi == 0 ? '+' : '-';
        SingleEndMapping(read_seqs[j], genome, hash_table, strand, AG_WILDCARD,
                         map_results[j]);
      }
    }
    //////////////////////////////////////////////////////////
    // Output
    for (uint32_t j = 0; j < num_of_reads; ++j) {
      if (map_results[j].times == 0) {
        num_of_unmapped++;
        if (unmapped) {
          OutputUnmapped(funmapped, read_names[j], read_seqs[j],
                         read_scores[j]);
        }
      } else if (map_results[j].times == 1) {
        num_of_unique_mapped++;
        OutputUniquelyAndAmbiguousMapped(fout, map_results[j], read_names[j],
                                         read_seqs[j], read_scores[j], genome);
      } else {
        num_of_ambiguous_mapped++;
        if (fambiguous) {
          OutputUniquelyAndAmbiguousMapped(fambiguous, map_results[j],
                                           read_names[j], read_seqs[j],
                                           read_scores[j], genome);
        }
      }
    }

    if (num_of_reads < n_reads_to_process)
      break;
  }
  fprintf(stderr, "]\n");

  fclose(fin);
  fclose(fout);
  if (ambiguous) {
    fclose(fambiguous);
  }
  if (unmapped) {
    fclose(funmapped);
  }

  fprintf(stderr, "[TOTAL NUMBER OF READS: %u]\n", num_of_total_reads);
  fprintf(stderr, "[UNIQUELY MAPPED READS: %u (%.2lf%%)]\n",
          num_of_unique_mapped,
          100.00 * num_of_unique_mapped / num_of_total_reads);
  fprintf(stderr, "[AMBIGUOUS MAPPED READS: %u (%.2lf%%)]\n",
          num_of_ambiguous_mapped,
          100.00 * num_of_ambiguous_mapped / num_of_total_reads);
  fprintf(stderr, "[UNMAPPED READS: %u (%.2lf%%)]\n", num_of_unmapped,
          100.00 * num_of_unmapped / num_of_total_reads);

  fprintf(stderr, "[MAPPING TAKES %.0lf SECONDS]\n",
          (double(clock() - start_t) / CLOCKS_PER_SEC));
}