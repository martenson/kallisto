#include "MinCollector.h"
#include <algorithm>

// utility functions

std::vector<int> intersect(const std::vector<int>& x, const std::vector<int>& y) {
  std::vector<int> v;
  auto a = x.begin();
  auto b = y.begin();
  while (a != x.end() && b != y.end()) {
    if (*a < *b) {
      ++a;
    } else if (*b < *a) {
      ++b;
    } else {
      v.push_back(*a);
      ++a;
      ++b;
    }
  }
  return v;
}


int MinCollector::collect(std::vector<std::pair<KmerEntry,int>>& v1,
                          std::vector<std::pair<KmerEntry,int>>& v2, bool nonpaired) {

  /*if (v1.empty()) {
    return -1;
  } else if (!nonpaired && v2.empty()) {
    return -1;
  }
  */

  std::vector<int> u1 = intersectECs(v1);
  std::vector<int> u2 = intersectECs(v2);

  std::vector<int> u;

  if (u1.empty() && u2.empty()) {
    return -1;
  }

  // non-strict intersection.
  if (u1.empty()) {
    if (v1.empty()) {
      u = u2;
    } else {
      return -1;
    }
  } else if (u2.empty()) {
    if (v2.empty()) {
      u = u1;
    } else {
      return -1;
    }
  } else {
    u = intersect(u1,u2);
  }

  if (u.empty()) {
    return -1;
  }
  return increaseCount(u);
}

int MinCollector::increaseCount(const std::vector<int>& u) {
  if (u.empty()) {
    return -1;
  }
  if (u.size() == 1) {
    int ec = u[0];
    ++counts[ec];
    return ec;
  }
  auto search = index.ecmapinv.find(u);
  if (search != index.ecmapinv.end()) {
    // ec class already exists, update count
    ++counts[search->second];
    return search->second;
  } else {
    // new ec class, update the index and count
    auto necs = counts.size();
    //index.ecmap.insert({necs,u});
    index.ecmap.push_back(u);
    index.ecmapinv.insert({u,necs});
    counts.push_back(1);
    return necs;
  }
}

int MinCollector::decreaseCount(const int ec) {
  assert(ec >= 0 && ec <= index.ecmap.size());
  --counts[ec];
  return ec;
}

struct ComparePairsBySecond {
  bool operator()(std::pair<KmerEntry,int> a, std::pair<KmerEntry,int> b) {
    return a.second < b.second;
  }
};

std::vector<int> MinCollector::intersectECs(std::vector<std::pair<KmerEntry,int>>& v) const {
  if (v.empty()) {
    return {};
  }
  sort(v.begin(), v.end(), [&](std::pair<KmerEntry, int> a, std::pair<KmerEntry, int> b)
       {
         if (a.first.contig==b.first.contig) {
           return a.second < b.second;
         } else {
           return a.first.contig < b.first.contig;
         }
       }); // sort by contig, and then first position


  int ec = index.dbGraph.ecs[v[0].first.contig];
  int lastEC = ec;
  std::vector<int> u = index.ecmap[ec];

  for (int i = 1; i < v.size(); i++) {
    if (v[i].first.contig != v[i-1].first.contig) {
      ec = index.dbGraph.ecs[v[i].first.contig];
      if (ec != lastEC) {
        u = index.intersect(ec, u);
        lastEC = ec;
        if (u.empty()) {
          return u;
        }
      }
    }
  }

  /*for (auto &x : vp) {
    //tmp = index.intersect(x.first,u);
    u = index.intersect(x.first,u);
    //if (!tmp.empty()) {
     // u = tmp;
      //count++; // increase the count
     // }
  }*/

  // if u is empty do nothing
  /*if (u.empty()) {
    return u;
    }*/

  // find the range of support
  int minpos = std::numeric_limits<int>::max();
  int maxpos = 0;

  for (auto& x : v) {
    minpos = std::min(minpos, x.second);
    maxpos = std::max(maxpos, x.second);
  }

  if ((maxpos-minpos + k) < min_range) {
    return {};
  }

  return u;
}


void MinCollector::loadCounts(ProgramOptions& opt) {
  int num_ecs = counts.size();
  counts.clear();
  std::ifstream in((opt.output + "/counts.txt"));
  int i = 0;
  if (in.is_open()) {
    std::string line;
    while (getline(in, line)) {
      std::stringstream ss(line);
      int j,c;
      ss >> j;
      ss >> c;
      if (j != i) {
        std::cerr << "Error: equivalence class does not match index. Found "
                  << j << ", expected " << i << std::endl;
        exit(1);
      }
      counts.push_back(c);
      i++;
    }

    if (i != num_ecs) {
      std::cerr << "Error: number of equivalence classes does not match index. Found "
                << i << ", expected " << num_ecs << std::endl;
      exit(1);
    }
  } else {
    std::cerr << "Error: could not open file " << opt.output << "/counts.txt" << std::endl;
    exit(1);

  }

}

double MinCollector::get_mean_frag_len() const {
  if (has_mean_fl) {
    return mean_fl;
  }
  
  auto total_counts = 0;
  double total_mass = 0.0;

  for ( size_t i = 0 ; i < flens.size(); ++i ) {
    total_counts += flens[i];
    total_mass += static_cast<double>(flens[i] * i);
  }

  if (total_counts == 0) {
    std::cerr << "Error: could not determine mean fragment length from paired end reads, no pairs mapped to a unique transcript." << std::endl
              << "       Run kallisto quant again with a pre-specified fragment length (option -l)." << std::endl;
    exit(1);
    
  }
  
  // cache the value
  const_cast<double&>(mean_fl) = total_mass / static_cast<double>(total_counts);
  const_cast<bool&>(has_mean_fl) = true;
  return mean_fl;
}


int hexamerToInt(const char *s, bool revcomp) {
  int hex = 0;
  if (!revcomp) {
    for (int i = 0; i < 6; i++) {
      hex <<= 2;
      switch (*(s+i) & 0xDF) {
      case 'A': break;
      case 'C': hex += 1; break;
      case 'G': hex += 2; break;
      case 'T': hex += 3; break;
      default: return -1;
      }    
    }
  } else {
    for (int i = 0; i < 6; i++) {
      switch (*(s+i) & 0xDF) {
      case 'A': hex += 3 << (2*i);break;
      case 'C': hex += 2 << (2*i); break;
      case 'G': hex += 1 << (2*i); break;
      case 'T': break;
      default: return -1;
      }    
    }
  }
  return hex;
}

bool MinCollector::countBias(const char *s1, const char *s2, const std::vector<std::pair<KmerEntry,int>> v1, const std::vector<std::pair<KmerEntry,int>> v2, bool paired) {

  const int pre = 2, post = 4;
  
  if (v1.empty() || (paired && v2.empty())) {
    return false;
  }


  
  auto getPreSeq = [&](const char *s, Kmer km, bool fw, bool csense,  KmerEntry val, int p) -> int {
    if (s==0) {
      return -1;
    }
    if ((csense && val.getPos() - p >= pre) || (!csense && (val.contig_length - 1 - val.getPos() - p) >= pre )) {
      const Contig &c = index.dbGraph.contigs[val.contig];
      bool sense = c.transcripts[0].sense;
      
      int hex = -1;
      //std::cout << "  " << s << "\n";
      if (csense) {
        hex = hexamerToInt(c.seq.c_str() + (val.getPos()-p - pre), true);
        //std::cout << c.seq.substr(val.getPos()- p - pre,6) << "\n";
      } else {
        int pos = (val.getPos() + p) + k - post;
        hex = hexamerToInt(c.seq.c_str() + (pos), false);
        //std::cout << revcomp(c.seq.substr(pos,6)) << "\n";
      }
      return hex;
    }

    return -1;
  };

  // find first contig of read
  KmerEntry val1 = v1[0].first;
  int p1 = v1[0].second;
  for (auto &x : v1) {
    if (x.second < p1) {
      val1 = x.first;
      p1 = x.second;
    }
  }
  
  Kmer km1 = Kmer((s1+p1));
  bool fw1 = (km1==km1.rep());
  bool csense1 = (fw1 == val1.isFw()); // is this in the direction of the contig?

  int hex5 = getPreSeq(s1, km1, fw1, csense1, val1, p1);

  /*
  int hex3 = -1;
  if (paired) {
    // do the same for the second read
    KmerEntry val2 = v2[0].first;
    int p2 = v2[0].second;
    for (auto &x : v2) {
      if (x.second < p2) {
        val2 = x.first;
        p2 = x.second;
      }
    }
    
    Kmer km2 = Kmer((s2+p2));
    bool fw2 = (km2==km2.rep());
    bool csense2 = (fw2 == val2.isFw()); // is this in the direction of the contig?
    
    hex3 = getPreSeq(s2, km2, fw2, csense2, val2, p2);
  }
  */
  
  if (hex5 >=0) { // && (!paired || hex3 >= 0)) {
    bias5[hex5]++;
    //bias3[hex3]++;
  } else {
    return false;
  }

  return false;
}
