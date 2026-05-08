#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include "json.hpp" // Include the external JSON library

using json = nlohmann::json;
using namespace std;

// Task 2: Set up a Gate class
class Gate {
public:
    int id;
    string type;
    vector<int> fanouts;

    // Default constructor
    Gate() {
        id = 0;
        type = "";
    }

    // Constructor to easily set up a gate
    Gate(int given_id, string given_type) {
        id = given_id;
        type = given_type;
    }

    // A simple function to print this gate's data into a file
    void printToFile(ofstream& file) {
        file << id << "\t" << type;
        
        // Loop through the fanouts list and print each one
        for (int i = 0; i < fanouts.size(); i++) {
            file << "\t" << fanouts[i];
        }
        file << "\n";
    }
};


// Function to process a single circuit file
void process_circuit(string circuit_name) {
    // Dynamically create the file paths pointing to the ../data/ folder
    string in_filename = "../data/" + circuit_name + ".json";
    string out_filename = "../data/" + circuit_name + "_output.txt";

    // 1. Open the JSON file
    ifstream my_file(in_filename);
    if (!my_file.is_open()) {
        cout << "  -> Error: Cannot open " << in_filename << "!" << endl;
        return; // Stop processing this specific file, but program continues
    }

    json json_data;
    my_file >> json_data;
    my_file.close();

    map<int, Gate> all_gates;
    int max_id = 0;

    // 2. Read data from JSON into our map
    for (int i = 0; i < json_data.size(); i++) {
        Gate temp_gate;
        temp_gate.id = json_data[i]["id"];
        temp_gate.type = json_data[i]["type"];
        
        for (int j = 0; j < json_data[i]["fanouts"].size(); j++) {
            int current_fanout = json_data[i]["fanouts"][j];
            temp_gate.fanouts.push_back(current_fanout);
        }
        
        all_gates[temp_gate.id] = temp_gate;
        
        if (temp_gate.id > max_id) {
            max_id = temp_gate.id;
        }
    }

    // 3. Find outputs and add "PO" gates
    vector<Gate> po_gates_list;

    for (auto const& pair : all_gates) {
        Gate current_gate = pair.second;

        if (current_gate.fanouts.size() == 0) {
            max_id = max_id + 1;
            all_gates[current_gate.id].fanouts.push_back(max_id);
            Gate new_po_gate(max_id, "PO");
            po_gates_list.push_back(new_po_gate);
        }
    }

    for (int i = 0; i < po_gates_list.size(); i++) {
        all_gates[po_gates_list[i].id] = po_gates_list[i];
    }

    // 4. Write everything to the output file in ../data/
    ofstream out_file(out_filename);
    if (!out_file.is_open()) {
        cout << "  -> Error: Cannot create " << out_filename << "!" << endl;
        return;
    }

    for (auto const& pair : all_gates) {
        Gate g = pair.second;
        g.printToFile(out_file);
    }

    out_file.close();
    cout << "  -> Success! Created " << out_filename << endl;
}


// Main function: Batch Process all circuits

int main() {
    // A list of all the ISCAS-85 circuit names we want to process
    vector<string> circuit_list = {
        "c17", "c432", "c499", "c880", "c1355", 
        "c1908", "c2670", "c3540", "c5315", "c6288", "c7552"
    };

    cout << "Starting C++ Task 3 Batch Processing..." << endl;
    cout << "----------------------------------------" << endl;

    // Loop through the list and process them one by one
    for (int i = 0; i < circuit_list.size(); i++) {
        cout << "Processing " << circuit_list[i] << "..." << endl;
        process_circuit(circuit_list[i]);
    }

    cout << "----------------------------------------" << endl;
    cout << "All files processed successfully!" << endl;

    return 0;
}