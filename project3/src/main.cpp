#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <sstream>  
#include <cstdint>  
#include <random>   
#include <iomanip>
#include "json.hpp" 
#include <functional>

using json = nlohmann::json;
using namespace std;

// Fault structure: Stores location, branch, and detection status
struct Fault {
    int gate_id;   
    int branch_id; // 0 represents a gate output fault, non-zero represents a specific input branch fault
    int stuck_at;  //fault type
    bool detected = false; // Used for Fault Dropping logic
};

// Gate class: Stores circuit node information
class Gate {
public:
    int id;
    string type;
    vector<int> fanouts;
    vector<int> fanins;    
    int level;             
    uint64_t good_value = 0;   
    uint64_t faulty_value = 0; 

    Gate() : id(0), type(""), level(-1) {}
    Gate(int given_id, string given_type) : id(given_id), type(given_type), level(-1) {}

    // Must be declared as const to be called in a constant reference loop
    void printToFile(ofstream& file) const {
        file << id << "\t" << type;
        for (int f : fanouts) file << "\t" << f;
        file << "\n";
    }
};

// ------------------------NEW------------------------------
// TPG (Test Pattern Generator) Class
// Implements a 32-bit LFSR with primitive polynomial:
// h(x) = x^32 + x^22 + x^2 + x^1 + 1
// ---------------------------------------------------------
class TPG {
private:
    uint32_t state; // 32-bit unsigned integer to store LFSR state

public:
    // Constructor: Initialize to 1010...1010 (0xAAAAAAAA)
    TPG() {
        state = 0xAAAAAAAA; 
    }

    // Get the current MSB as scan-in bit and advance LFSR state
    uint8_t next_bit() {
        // 1. Extract the bit to be fed into the scan chain (MSB S[31])
        uint8_t scan_bit = (state >> 31) & 1;

        // 2. Calculate feedback bit based on the polynomial: 
        // feedback = S[31] XOR S[21] XOR S[1] XOR S[0]
        uint32_t feedback = ((state >> 31) ^ 
                             (state >> 21) ^ 
                             (state >> 1)  ^ 
                             (state & 1)) & 1;

        // 3. Left shift state by 1 and put feedback at LSB S[0]
        state = (state << 1) | feedback;

        return scan_bit;
    }

    // Reset function (used to reset state before processing the next circuit)
    void reset() {
        state = 0xAAAAAAAA;
    }
};

// ---------------------------------------------------------
// ORA (Output Response Analyzer) Class
// Implements a 16-bit serial MISR with primitive polynomial:
// h(x) = x^16 + x^15 + x^13 + x^4 + 1
// ---------------------------------------------------------
class ORA {
private:
    uint16_t state; // 16-bit unsigned integer to store ORA state

public:
    // Constructor: Initialize to all 0s (0x0000)
    ORA() {
        state = 0x0000;
    }

    // Read one bit scanned out from the circuit and update ORA signature
    void update(uint8_t b) {
        // Ensure the input bit is only 0 or 1
        b = b & 1;

        // 1. Calculate feedback bit based on the polynomial:
        // feedback = R[15] XOR R[14] XOR R[12] XOR R[3] XOR b
        uint16_t feedback = ((state >> 15) ^ 
                             (state >> 14) ^ 
                             (state >> 12) ^ 
                             (state >> 3)  ^ 
                             b) & 1;

        // 2. Left shift state by 1 and put feedback at LSB R[0]
        state = (state << 1) | feedback;
    }

    // Get the current 16-bit signature
    uint16_t get_signature() const {
        return state;
    }

    // Reset function
    void reset() {
        state = 0x0000;
    }
};

// Core bitwise function: Computes 64-bit logic result based on gate type
uint64_t calculate_bitwise(string type, const vector<uint64_t>& inputs) {
    if (inputs.empty() && type != "PI") return 0;
    if (type == "NOT") return ~inputs[0];
    if (type == "BUFF" || type == "PO") return inputs[0];
    if (type == "AND") { uint64_t res = ~0ULL; for (uint64_t v : inputs) res &= v; return res; }
    if (type == "NAND") { uint64_t res = ~0ULL; for (uint64_t v : inputs) res &= v; return ~res; }
    if (type == "OR") { uint64_t res = 0ULL; for (uint64_t v : inputs) res |= v; return res; }
    if (type == "NOR") { uint64_t res = 0ULL; for (uint64_t v : inputs) res |= v; return ~res; }
    if (type == "XOR") { uint64_t res = inputs[0]; for (size_t i = 1; i < inputs.size(); ++i) res ^= inputs[i]; return res; }
    if (type == "XNOR") { uint64_t res = inputs[0]; for (size_t i = 1; i < inputs.size(); ++i) res ^= inputs[i]; return ~res; }
    return 0;
}

// Helper function to format fault strings for the output file
string format_fault(const Fault& f) {
    return to_string(f.gate_id) + "_" + to_string(f.branch_id) + "_SA" + to_string(f.stuck_at);
}

void process_circuit(string circuit_name) {
    // FIXED: Using actual local file extensions based on your previous successful runs
    string in_filename = "../data/" + circuit_name + ".json"; 
    string fault_filename = "../data/" + circuit_name + "_faults.txt"; 
    string out_filename = "../data/" + circuit_name + "_results.txt";

    // 1. Parse Circuit Netlist (JSON)
    ifstream my_file(in_filename);
    if (!my_file.is_open()) { cout << "  -> Error: Cannot open " << in_filename << endl; return; }
    json json_data;
    my_file >> json_data;
    my_file.close();

    map<int, Gate> all_gates;
    int max_id = 0;
    vector<int> pi_ids, po_ids;

    for (size_t i = 0; i < json_data.size(); i++) {
        int gid = json_data[i]["id"];
        all_gates[gid] = Gate(gid, json_data[i]["type"]);
        for (int f : json_data[i]["fanouts"]) all_gates[gid].fanouts.push_back(f);
        if (gid > max_id) max_id = gid;
        if (all_gates[gid].type == "PI") pi_ids.push_back(gid);
    }

    // 2. Identify and add PO nodes safely
    vector<int> needs_po;
    for (auto const& [id, g] : all_gates) { if (g.fanouts.empty()) needs_po.push_back(id); }
    for (int id : needs_po) {
        int new_po_id = ++max_id;
        all_gates[id].fanouts.push_back(new_po_id);
        all_gates[new_po_id] = Gate(new_po_id, "PO");
        po_ids.push_back(new_po_id);
    }

    // 3. Build fanin lists
    for (auto const& [id, g] : all_gates) {
        for (int tid : g.fanouts) all_gates[tid].fanins.push_back(id);
    }

    // 4. Topological sorting (Levelization)
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [id, g] : all_gates) {
            if (g.level != -1) continue;
            if (g.type == "PI") { g.level = 0; changed = true; }
            else if (!g.fanins.empty()) {
                bool ready = true; int ml = -1;
                for (int fin : g.fanins) {
                    if (all_gates[fin].level == -1) { ready = false; break; }//all finin should be levelized
                    ml = max(ml, all_gates[fin].level);
                }
                if (ready) { g.level = ml + 1; changed = true; }
            }
        }
    }
    
    // 5. Read fault list
    vector<Fault> fault_list;
    ifstream f_file(fault_filename);
    if (f_file.is_open()) {
        string line;
        while (getline(f_file, line)) {
            stringstream ss(line); Fault f;
            if (ss >> f.gate_id >> f.branch_id >> f.stuck_at) fault_list.push_back(f);
        }
        f_file.close();
    } else {
        cout << "  -> Error: Cannot open fault file " << fault_filename << endl;
        return; // Stop processing this circuit if faults file is missing
    }

    // 6. Sort execution queue by level
    vector<Gate*> eval_queue;
    for (auto& [id, g] : all_gates) eval_queue.push_back(&g);
    sort(eval_queue.begin(), eval_queue.end(), [](Gate* a, Gate* b){ return a->level < b->level; });

    //new
    // Scan-in shift: lowest numbered input -> highest numbered input 
    sort(pi_ids.begin(), pi_ids.end()); 
    
    // Output scan shift: highest numbered output -> lowest numbered output 
    sort(po_ids.begin(), po_ids.end(), greater<int>()); 

    TPG tpg;
    ORA ora_good;

    // 2. Simulate 100 patterns on good circuit 
    for (int p = 1; p <= 100; ++p) { 
        
        // Scan-in: Shift bits from TPG into inputs 
        for (int pi_id : pi_ids) {
            all_gates[pi_id].good_value = tpg.next_bit(); 
        }

        // Capture: Evaluate circuit logic (eval_queue is already level-sorted) 
        for (Gate* g : eval_queue) {
            if (g->type != "PI") {
                vector<uint64_t> iv;
                for (int fin : g->fanins) {
                    iv.push_back(all_gates[fin].good_value);
                }
                g->good_value = calculate_bitwise(g->type, iv);
            }
        }

        // Scan-out: Shift outputs into ORA to update signature 
        for (int po_id : po_ids) {
            uint8_t out_bit = all_gates[po_id].good_value & 1; 
            ora_good.update(out_bit); 
        }
    }

    //Compute and store the final ORA signature for good circuit
    uint16_t good_signature = ora_good.get_signature();
    cout << "  -> Good ORA Signature for " << circuit_name << ": 0x" 
         << hex << uppercase << setfill('0') << setw(4) << good_signature 
         << dec << endl;

    // Open output file for writing Deliverable results
    ofstream res_file(out_filename);
    if (!res_file.is_open()) {
        cout << "  -> Error: Cannot create output file " << out_filename << endl;
        return;
    }

    // ---------------------------------------------------------
    // Phase 2: Parallel Pattern Fault Simulation (Word-Level Packing)
    // ---------------------------------------------------------
    int detected_count = 0;
    int alias_count = 0; // Track how many times Aliasing occurs

    // Process faults in chunks of 64
    for (size_t f_base = 0; f_base < fault_list.size(); f_base += 64) {
        int chunk_size = min((size_t)64, fault_list.size() - f_base);
        
        TPG tpg_faulty;            // Automatically resets to 0xAAAAAAAA for each chunk
        ORA faulty_oras[64];       // Array of 64 independent ORAs initialized to 0
        bool propagated[64] = {false}; // Tracks if a fault ever changed a PO bit

        // Run 100 sequential patterns for this chunk of 64 faults
        for (int p = 1; p <= 100; ++p) {
            
            // Scan-in: Get 1 bit from TPG, expand to 64 bits for parallel logic
            for (int pi_id : pi_ids) {
                uint64_t bit = tpg_faulty.next_bit();
                all_gates[pi_id].good_value = bit;
                all_gates[pi_id].faulty_value = bit ? ~0ULL : 0ULL; 
            }

            // Capture: Evaluate logic & Inject faults
            for (Gate* g : eval_queue) {
                if (g->type != "PI") {
                    // Good value eval (needed to compare against for Aliasing check)
                    vector<uint64_t> iv_good;
                    for (int fin : g->fanins) iv_good.push_back(all_gates[fin].good_value);
                    g->good_value = calculate_bitwise(g->type, iv_good) & 1;

                    // Faulty value eval with 64-bit packing
                    vector<uint64_t> iv_faulty;
                    for (int fin : g->fanins) {
                        uint64_t v = all_gates[fin].faulty_value;
                        // Inject branch faults
                        for (int i = 0; i < chunk_size; ++i) {
                            Fault& f = fault_list[f_base + i];
                            if (g->id == f.gate_id && fin == f.branch_id) {
                                if (f.stuck_at == 1) v |= (1ULL << i);
                                else v &= ~(1ULL << i);
                            }
                        }
                        iv_faulty.push_back(v);
                    }
                    
                    uint64_t gv = calculate_bitwise(g->type, iv_faulty);
                    
                    // Inject gate output faults
                    for (int i = 0; i < chunk_size; ++i) {
                        Fault& f = fault_list[f_base + i];
                        if (g->id == f.gate_id && f.branch_id == 0) {
                            if (f.stuck_at == 1) gv |= (1ULL << i);
                            else gv &= ~(1ULL << i);
                        }
                    }
                    g->faulty_value = gv;
                }
            }

            // Scan-out: Update 64 ORAs and check for propagation
            for (int po_id : po_ids) {
                uint8_t good_out_bit = all_gates[po_id].good_value & 1;
                uint64_t faulty_out_word = all_gates[po_id].faulty_value;
                
                for (int i = 0; i < chunk_size; ++i) {
                    uint8_t faulty_bit = (faulty_out_word >> i) & 1;
                    faulty_oras[i].update(faulty_bit); // Serially update specific fault's ORA
                    
                    if (faulty_bit != good_out_bit) {
                        propagated[i] = true; // Fault successfully altered a PO bit!
                    }
                }
            }
        } // End of 100 patterns for current chunk

        // Finalize chunk: Check detection and aliasing
        for (int i = 0; i < chunk_size; ++i) {
            if (faulty_oras[i].get_signature() != good_signature) {
                // Signature changed -> Fault Detected!
                fault_list[f_base + i].detected = true;
                detected_count++;
            } else if (propagated[i] == true) {
                // Fault changed POs, but signature is same -> ALIASING!
                alias_count++;
            }
        }
    }

    // ---------------------------------------------------------
    // Phase 3: Write Results to File and Console
    // ---------------------------------------------------------
    res_file << "Circuit: " << circuit_name << "\n";
    res_file << "Good ORA Signature: 0x" << hex << uppercase << setfill('0') << setw(4) << good_signature << dec << "\n";
    res_file << "Total Faults: " << fault_list.size() << "\n";
    res_file << "Detected Faults: " << detected_count << "\n";
    res_file << "Fault Coverage: " << fixed << setprecision(2) << ((double)detected_count / fault_list.size()) * 100 << "%\n";
    res_file << "Observed Aliasing Count: " << alias_count << "\n";
    res_file.close();

    // Print final console output
    cout << "  -> Total Faults: " << fault_list.size() << " | Detected: " << detected_count 
         << " (" << fixed << setprecision(2) << ((double)detected_count/fault_list.size())*100 << "%)";
    if (alias_count > 0) cout << " [Aliasing Occurred: " << alias_count << " times]";
    cout << endl;
}

int main() {
    // Exactly 5 benchmark circuits required by Deliverable 1
    vector<string> circuits = {"c17", "c432", "c499", "c880", "c1355"};
    
    cout << "Starting VLSI Project 2 - Final PPSFP Simulator" << endl;
    cout << "---------------------------------------------------" << endl;
    for (const string& c : circuits) {
        cout << "Processing " << c << "..." << endl;
        process_circuit(c);
    }
    cout << "---------------------------------------------------" << endl;
    cout << "Simulation Finished Successfully!" << endl;
    return 0;
}