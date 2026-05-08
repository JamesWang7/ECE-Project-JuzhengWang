import json
import os
#opne file and read only the keyword 'with' can make sure file closed securely
def parser(filepath):
    #dictionary to store the source of wires
    wire_to_source = {}
    #dictionary to store the information of gates
    gates = {}
    #dictionary to store the input id for current gate
    gate_input = {}
    with open(filepath,'r') as file:
        lines = file.readlines()
    current_gate = None
    inputs_to_read = 0
    for line in lines:
        #read file by lines
        clean_line = line.strip()
        #skip spaces and stars 
        if clean_line == " " or clean_line.startswith('*'):
            continue
        #splite the line into seperate parts
        parts = clean_line.split()
        if inputs_to_read == 0:
            #assign fist char to line_id
            line_id = int(parts[0])
            if "fan from" in clean_line:
                source_id = int(parts[3].replace('gat',''))
                wire_to_source[line_id] = source_id
            else:
                gate_type = parts[2].upper()
                #source of a gate is itself
                wire_to_source[line_id] = line_id
                if gate_type == 'INPT':
                    #change INPT to PI
                    gate_type = "PI"
                
                #gate information
                gates[line_id] = {
                    "id" : line_id,
                    "type" : gate_type,
                    "fanouts" : []
                }    
                gate_input[line_id] = []
                fanin_counts = int(parts[4])  
                if fanin_counts > 0 :
                    current_gate = line_id
                    inputs_to_read = fanin_counts
                continue
        if inputs_to_read > 0:
            for part in parts:
                #input_id of current gate
                input_id = int(part)
                gate_input[current_gate].append(input_id)
                inputs_to_read -= 1
    #use Tuple Unpacking
    for id,input in gate_input.items():
        for input_id in input:
            #find authetic source for fan in
            authetic_wire_to_source = wire_to_source.get(input_id,input_id)
            gates[authetic_wire_to_source]["fanouts"].append(id)

    for gate in gates.values():
        gate["fanouts"].sort()

        # Create a final list of gates, sorted by their ID
        result_list = []
        sorted_gate_ids = sorted(gates.keys())
        
        for gid in sorted_gate_ids:
            result_list.append(gates[gid])
            
    return result_list
#        return gate_input

if __name__ == "__main__":
    input_file = "c17.isc"
    output_file = "c17.json"
    #check the existence of input_file
    if os.path.exists(input_file):
        parsed_data = parser(input_file)
        with open(output_file, 'w') as json_file:
            json.dump(parsed_data, json_file, indent=4)
        print(f"Success! Parsed {input_file} into{output_file}")
    else:
        print(f"Error: Could not find {input_file}. Please check the path.")