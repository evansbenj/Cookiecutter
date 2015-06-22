#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <list>
#include <map>
#include <algorithm>
#include <locale>
#include <getopt.h>
#include <stdlib.h>
#include <unordered_map>

#include "fileroutines.h"
#include "readroutines.h"
#include "search.h"
#include "stats.h"
#include "seq.h"

/*! \brief Read adapter patterns from an input stream
 *
 *  \param[in]  kmers_f     an input stream to read the patterns
 *  \param[in]  polyG       the length of poly-G and poly-C patterns
 *  \param[in]  patters     the vector to which the patterns are written
 */
void build_patterns(std::ifstream & kmers_f, int polyG, std::vector <std::pair <std::string, Node::Type> > & patterns)
{
    std::string tmp;
    while (!kmers_f.eof()) {
        std::getline(kmers_f, tmp);
        std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::toupper);
        if (!tmp.empty()) {
            size_t tab = tmp.find('\t');
            if (tab == std::string::npos) {
                patterns.push_back(std::make_pair(tmp, Node::Type::adapter));
            } else {
                patterns.push_back(std::make_pair(tmp.substr(0, tab), Node::Type::adapter));
            }
        }
    }
    kmers_f.close();
    patterns.push_back(std::make_pair("NN", Node::Type::n));
    if (polyG) {
        patterns.push_back(std::make_pair(std::string(polyG, 'G'), Node::Type::polyG));
        patterns.push_back(std::make_pair(std::string(polyG, 'C'), Node::Type::polyC));
    }
}

/*! \brief Filter single-end reads by patterns
 *
 *  \param[in]  reads_f     an input stream of read sequences
 *  \param[out] ok_f        an output stream of filtered reads
 *  \param[out] stats       statistics on processed reads
 *  \param[out] root        a root of the trie structure used to perform string matching
 *  \param[in]  patterns    a vector of patterns for read filtration
 *  \param[in]  length      the read length threshold
 *  \param[in]  dust_k      the DUST algorithm parameter
 *  \param[in]  dust_cutoff the DUST score threshold
 *  \param[in]  errors      the number of resolved mismatches between a read and 
 *                          a pattern
 */
void filter_single_reads(std::ifstream & reads_f, std::ofstream & ok_f, 
                         Stats & stats, Node * root, std::vector <std::pair<std::string, Node::Type> > const & patterns,
                         int length, int dust_k, int dust_cutoff, int errors)
{
    Seq read;
    int processed = 0;

    while (read.read_seq(reads_f)) {
        ReadType type = check_read(read.seq, root, patterns, length, dust_k, dust_cutoff, errors);
        stats.update(type);
        if (type == ReadType::ok) {
            read.write_seq(ok_f);
        }

        processed += 1;
        if (processed % 1000000 == 0) {
            std::cout << "Processed: " << processed << std::endl;
        }
    }
}

/*! \brief Filter paired-end reads by patterns
 *
 *  \param[in]  reads1_f    an input stream of paired-end read 1 sequences
 *  \param[in]  reads2_f    an input stream of paired-end read 2 sequences
 *  \param[out] ok1_f       an output stream to write filtered paired-end read 1 sequences to
 *  \param[out] ok2_f       an output stream to write filtered paired-end read 2 sequences to
 *  \param[out] stats1      statistics on first parts of processed reads
 *  \param[out] stats2      statistics on second parts of processed reads
 *  \param[in]  root        a root of the trie structure used to perform string matching
 *  \param[in]  patterns    a vector of patterns for read filtration
 *  \param[in]  length      the read length threshold
 *  \param[in]  dust_k      the DUST algorithm parameter
 *  \param[in]  dust_cutoff the DUST score threshold
 *  \param[in]  errors      the number of resolved mismatches between a read and
 *                          a pattern
 */
void filter_paired_reads(std::ifstream & reads1_f, std::ifstream & reads2_f,
                         std::ofstream & ok1_f, std::ofstream & ok2_f,
                         Stats & stats1, Stats & stats2,
                         Node * root, std::vector <std::pair<std::string, Node::Type> > const & patterns,
                         int length, int dust_k, int dust_cutoff, int errors)
{
    Seq read1;
    Seq read2;
    int processed = 0;

    while (read1.read_seq(reads1_f) && read2.read_seq(reads2_f)) {
        ReadType type1 = check_read(read1.seq, root, patterns, length, dust_k, dust_cutoff, errors);
        ReadType type2 = check_read(read2.seq, root, patterns, length, dust_k, dust_cutoff, errors);
        if (type1 == ReadType::ok && type2 == ReadType::ok) {
            read1.write_seq(ok1_f);
            read2.write_seq(ok2_f);
            stats1.update(type1, true);
            stats2.update(type2, true);
        } else {
            stats1.update(type1, false);
            stats2.update(type2, false);
        }

        processed += 1;
        if (processed % 1000000 == 0) {
            std::cout << "Processed: " << processed << std::endl;
        }
    }
}

/*! \brief Print program parameters */
void print_help() 
{
    std::cout << "./rm_reads [-i raw_data.fastq | -1 raw_data1.fastq -2 raw_data2.fastq] -o output_dir --polyG 13 --length 50 --fragments fragments.dat --dust_cutoff cutoff --dust_k k -e 1" << std::endl;
}

/*! \brief The main function of the **remove** tool. */
int main(int argc, char ** argv)
{
    Node root('0');
    std::vector <std::pair<std::string, Node::Type> > patterns;

    std::string kmers, reads, out_dir;
    std::string reads1, reads2;
    char rez = 0;
    int length = 0;
    int polyG = 0;
    int dust_k = 4;
    int dust_cutoff = 0;
    int errors = 0;

    const struct option long_options[] = {
        {"length",required_argument,NULL,'l'},
        {"polyG",required_argument,NULL,'p'},
        {"fragments",required_argument,NULL,'a'},
        {"dust_k",required_argument,NULL,'k'},
        {"dust_cutoff",required_argument,NULL,'c'},
        {"errors",required_argument,NULL,'e'},
        {NULL,0,NULL,0}
    };

    while ((rez = getopt_long(argc, argv, "1:2:l:p:a:i:o:e:", long_options, NULL)) != -1) {
        switch (rez) {
        case 'l':
            length = std::atoi(optarg);
            break;
        case 'p':
            polyG = std::atoi(optarg);
            // polyG = boost::lexical_cast<int>(optarg);
            break;
        case 'a':
            kmers = optarg;
            break;
        case 'i':
            reads = optarg;
            break;
        case '1':
            reads1 = optarg;
            break;
        case '2':
            reads2 = optarg;
            break;
        case 'o':
            out_dir = optarg;
            break;
        case 'c':
            dust_cutoff = std::atoi(optarg);
            break;
        case 'k':
            dust_k = std::atoi(optarg);
            break;
        case 'e':
            errors = std::atoi(optarg);
            break;
        case '?':
            print_help();
            return -1;
        }
    }

    if (errors < 0 || errors > 2) {
        std::cout << "possible errors count are 0, 1, 2" << std::endl;
        return -1;
    }

    if (kmers.empty() || out_dir.empty() || (
            reads.empty() &&
            (reads1.empty() || reads2.empty()))) {
        print_help();
        return -1;
    }

    if (!verify_directory(out_dir)) {
        std::cout << "Output directory does not exist, failed to create" << std::endl;
    }

    std::ifstream kmers_f (kmers.c_str());
    if (!kmers_f.good()) {
        std::cout << "Cannot open kmers file" << std::endl;
        print_help();
        return -1;
    }

    init_type_names(length, polyG, dust_k, dust_cutoff);

    build_patterns(kmers_f, polyG, patterns);

    /*
    for (std::vector <std::string> ::iterator it = patterns.begin(); it != patterns.end(); ++it) {
        std::cout << *it << std::endl;
    }
    */

    if (patterns.empty()) {
        std::cout << "patterns are empty" << std::endl;
        return -1;
    }

    std::cout << "Building trie..." << std::endl;
    build_trie(root, patterns, errors);
	add_failures(root);

    if (!reads.empty()) {
        std::string reads_base = basename(reads);
        std::ifstream reads_f (reads.c_str());
        std::ofstream ok_f((out_dir + "/" + reads_base + ".ok.fastq").c_str(), std::ofstream::out);
        // std::ofstream bad_f((out_dir + "/" + reads_base + ".filtered.fastq").c_str(), std::ofstream::out);

        if (!reads_f.good()) {
            std::cout << "Cannot open reads file" << std::endl;
            print_help();
            return -1;
        }

        if (!ok_f.good()) {
            std::cout << "Cannot open output file" << std::endl;
            print_help();
            return -1;
        }

        Stats stats(reads);

        filter_single_reads(reads_f, ok_f, stats, &root, patterns, length, dust_k, dust_cutoff, errors);

        std::cout << stats;

        ok_f.close();
        reads_f.close();
    } else {
        std::string reads1_base = basename(reads1);
        std::string reads2_base = basename(reads2);
        std::ifstream reads1_f(reads1.c_str());
        std::ifstream reads2_f(reads2.c_str());
        std::ofstream ok1_f((out_dir + "/" + reads1_base + ".ok.fastq").c_str(),
                            std::ofstream::out);
        std::ofstream ok2_f((out_dir + "/" + reads2_base + ".ok.fastq").c_str(),
                            std::ofstream::out);
        
        if (!reads1_f.good() || !reads2_f.good()) {
            std::cout << "reads file is bad" << std::endl;
            print_help();
            return -1;
        }

        if (!ok1_f.good() || !ok2_f.good()) {
            std::cout << "out file is bad" << std::endl;
            print_help();
            return -1;
        }

        Stats stats1(reads1);
        Stats stats2(reads2);

        filter_paired_reads(reads1_f, reads2_f, ok1_f, ok2_f,
                            stats1, stats2,
                            &root, patterns, length, dust_k, dust_cutoff, errors);

        std::cout << stats1;
        std::cout << stats2;

        ok1_f.close();
        ok2_f.close();
        reads1_f.close();
        reads2_f.close();
    }
}
