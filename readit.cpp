/*
 * Only 16 bit PCM MONO supported now.
 */
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <cstdio>
#include <map>
#include <set>
#include <list>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>

#define BUFFERSIZE 1 // Number of sample rates (ex. seconds)
#define MINWAVEFREQ 20 // Minimum frequency that we want to process
#define MAXSTEPS 2 // How many steps to skip when we are looking for silences'
#define MATCHME 7 // Matching this number of silences means success

using namespace std;

FILE * process_headers(const char*);
bool check_sample(const char*, const char*);
void dump_values(FILE * in);
void read_periods(const char*);
void read_cfg(const char*);
void fatal (const char* );

char errmsg[200]; // Temporary message
int SAMPLE_RATE, // 
	DATA_SIZE,  // Data chunk size in bytes
	PROC_SAMPLES; // Minimum number of samples to process and be sure two waves included

map<string, string> cfg; // OK, ok... Is it possible to store any value but string (Rrrr...) here?

class Range {
	public:
		Range() { tm = tmin = tmax = 0; };
		Range(const double & a) { tm = a; tmin = a * 0.9; tmax = a * 1.1; }
		Range operator = (const double i) { return Range(i); }
		bool operator == (const double & i) const { return tmin < i && i < tmax; }
		bool operator == (const Range & r) const { return r == tm; }
		bool operator > (const double & i) const { return i > tmax; }
		bool operator > (const Range & r) const { return r > tm; }
		bool operator < (const double & i) const { return i < tmin; }
		bool operator < (const Range & r) const { return r < tm; }
	protected:
		int tmin, tm, tmax;
};
typedef list<pair<int,int> > ttrace;
class workitm {
	public:
		int len,a,b;
		workitm(int a, int b) { len = 0; this->a = a; this->b = b; }
		int ami(int, int);
		ttrace trace;
};

typedef multimap<Range,pair<int,double> > tvals;
typedef list<workitm> twork;

// ------------------------------------------------------------
// This method finds a "good" value in the set and
// returns:
// 0 if element deleted
// 1 if it's a good element
// 2 if the search is over (pattern recognized)
//
int workitm::ami(const int a, const int b) {
	if (a - this->a > MAXSTEPS || this->b - b > MAXSTEPS) return 0;
	// ------------------------------------------------------------
	// Here we have a nice thing called success
	// We fit the "region" here. We either finished,
	// or just increasing len
	//
	// Hemm we should add some trace stuff here
	this->trace.push_back(pair<int,int>(a,b));

	return (++this->len < MATCHME)?1:2;
}

int main (int argc, char *argv[]) {

	if (argc < 3) {
		fatal ("Usage: ./a.out config.cfg sample.wav\n\nor\n"
				"./a.out config.cfg samplefile.txt catchable.wav");
	}
	read_cfg(argv[1]);
	// ------------------------------------------------------------
	// Only header given, we just dump out the silence values
	//
	if (argc == 3) {
		FILE * in = process_headers(argv[2]);
		dump_values(in);
	}
	// ------------------------------------------------------------
	// We have values file and a new samplefile. Let's rock
	//
	else if (argc == 4) {
		tvals vals;

		ifstream file;
		file.open(argv[2]);
		string line;
		int x;
		double tmp2;
		for (int i = 0; ! file.eof(); i++) {
			getline(file, line);
			x = line.find(";");
			if (x == -1) break;
			istringstream place(line.substr(0,x));
			istringstream range(line.substr(x+1));
			pair<Range,pair<int,double> > tmp;
			range >> tmp2;
			tmp.first = tmp2;
			place >> tmp.second.second;
			tmp.second.first = i;
			vals.insert(tmp);
		}
		// OK, we have it. Let's print?
		//for (vector<trange>::iterator it1 = vals.begin(); it1 != vals.end(); it1++) {
		//printf ("%.6f - %.6f\n", (*it1).first, (*it1).second);
		//}
		// ------------------------------------------------------------
		// Let's work with the seconds sample like with a stream
		//
		FILE * an = process_headers(argv[3]);
		// We can read and analyze it now
		// Trying to match in only first 15 seconds of the record

		short int buf [SAMPLE_RATE * BUFFERSIZE]; // Process buffer every second
		istringstream i(cfg["treshold1"]);
		double treshold1;
		i >> treshold1;
		int min_silence = (int)((1 << 15) * treshold1), // Below this value we have "silence, pshhh..."
			found_s = 0,
			b = -1, // This is the found silence counter (starting with zero)
			head = 0;
		twork work;
		while (! feof(an) ) {
			fread(buf, 2, SAMPLE_RATE * BUFFERSIZE, an);
			for (int i = 0; i < SAMPLE_RATE * BUFFERSIZE; i++) {
				int cur = abs(buf[i]);

				if (cur <= min_silence) {
					found_s++;
				} else {
					if (found_s >= PROC_SAMPLES) {
						b++; // Increasing the number of silence counter (sort of current index)
						// Found a found_s/SAMPLE_RATE length silence
						// Let's look for an analogous thing in vals variable (original file)
						double sec = (double)found_s/SAMPLE_RATE;
						// Find sec in range
						pair<tvals::iterator, tvals::iterator> pa = vals.equal_range(sec);

						//------------------------------------------------------------
						// We put a indexes here that we use for continued threads
						// (we don't want to create a new "thread" with already
						// used length of a silence)
						//
						set<int> used_a; 

						//------------------------------------------------------------
						// Iterating through samples that match the found silence
						//
						for (tvals::iterator in_a = pa.first; in_a != pa.second; in_a++)
						{
							int a = (*in_a).second.first;
							/*
							   printf("%7.4f<->%7.4f,     %.4f<->%.4f (%5.2f%%)\n",
							   vals[i].first, (double)(head-found_s)/SAMPLE_RATE,
							   vals[i].second.tm, sec,
							   vals[i].second.tm *100/sec-100);
							   */
							//------------------------------------------------------------
							// Check if it exists in our work array
							//
							for (twork::iterator w = work.begin(); w != work.end(); w++) {
								int r = w->ami(b, a);
								switch (r) {
									case 0: work.erase(w); break;
									case 1: used_a.insert(a); break;
									case 2: // We finished our work. Print the trace.
										printf("Found a matching pattern! Length: %d, here comes the trace:\n", w->trace.size());
										for (ttrace::iterator tr = w->trace.begin(); tr != w->trace.end(); tr++) {
											printf("%d %d\n", tr->first, tr->second);
										}
								}
							}
							if (used_a.find(a) != used_a.end()) {
								work.push_back(workitm(a,b));
							}
						}
						// Looking for another silence
						found_s = 0;
					}
				}
				head++;
			}
		}
	}
	exit(0);
}

void read_cfg (const char * filename) {
	ifstream file;
	file.open(filename);
	string line;
	int x;
	while (! file.eof() ) {
		getline(file, line);
		x = line.find(":");
		if (x == -1) break; // Last line, exit
		cfg[line.substr(0,x)] = line.substr(x+2);
	}
}

FILE * process_headers(const char * infile) {
	FILE * in = fopen(infile, "rb");

	// Read bytes [0-3] and [8-11], they should be "RIFF" and "WAVE" in ASCII
	char header[5];
	fread(header, 1, 4, in);

	char sample[] = "RIFF";
	if (! check_sample(sample, header) ) {
		fatal ("RIFF header not found, exiting\n");
	}
	// Checking for compression code (21'st byte is 01, 22'nd - 00, little-endian notation
	short int tmp[2]; // two-byte integer
	fseek(in, 0x14, 0); // offset = 20 bytes
	fread(&tmp, 2, 2, in); // Reading two two-byte samples (comp. code and no. of channels)
	if ( tmp[0] != 1 ) {
		sprintf(errmsg, "Only PCM(1) supported, compression code given: %d\n", tmp[0]);
		fatal (errmsg);
	}
	// Number of channels must be "MONO"
	if ( tmp[1] != 1 ) {
		printf(errmsg, "Only MONO supported, given number of channels: %d\n", tmp[1]);
		fatal (errmsg);
	}
	fread(&SAMPLE_RATE, 2, 1, in); // Single two-byte sample
	//printf ("Sample rate: %d\n", SAMPLE_RATE);
	PROC_SAMPLES = SAMPLE_RATE / MINWAVEFREQ;
	fseek(in, 0x24, 0);
	fread(header, 1, 4, in);
	strcpy(sample, "data");
	if (! check_sample(sample, header)) {
		fatal ("data chunk not found in byte offset=36, file corrupted.");
	}
	// Get data chunk size here
	fread(&DATA_SIZE, 4, 1, in); // Single two-byte sample
	//printf ("Datasize: %d bytes\n", DATA_SIZE);
	return in;
}

bool check_sample (const char * sample, const char * b) { // My STRCPY.
	for(int i = 0; i < sizeof(sample)-1; i++) {
		if (sample[i] != b[i]) {
			return false;
		}
	}
	return true;
}

void fatal (const char * msg) { // Print error message and exit
	perror (msg);
	exit (1);
}

void dump_values(FILE * in) {
	istringstream i(cfg["treshold1"]);
	double treshold1;
	i >> treshold1;
	// ------------------------------------------------------------
	// This "15" means we have 16 bits for signed, this means
	// 15 bits for unsigned int.
	int min_silence = (int)((1 << 15) * treshold1); // Below this value we have "silence, pshhh..."

	short int buf [SAMPLE_RATE * BUFFERSIZE]; // Process buffer every second
	int head = 0, // Assume sample started
		found_s = 0; // Number of silence samples

	int first_silence = 0;

	while (! feof(in) ) {
		fread(buf, 2, SAMPLE_RATE * BUFFERSIZE, in);
		for (int i = 0; i < SAMPLE_RATE * BUFFERSIZE; i++) {
			int cur = abs(buf[i]);

			if (cur <= min_silence) {
				found_s++;
			} else {
				if (found_s >= PROC_SAMPLES) {
					printf("%.6f;%.6f\n", (double)(head-found_s)/SAMPLE_RATE, (double)found_s/SAMPLE_RATE);
				}
				found_s = 0;
			}
			head++;
		}
	}
}
