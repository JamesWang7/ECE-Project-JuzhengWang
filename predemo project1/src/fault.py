import json
import os
import glob

# open file and read. The keyword 'with' makes sure file closed securely
def parser(filepath):
    # dictionary to store the source of wires
    wire_to_source = {}
    # dictionary to store the information of gates
    gates = {}
    # dictionary to store the input id for current gate
    gate_input = {}
    
    # dictionary to store faults like [0, 1] for each line
    line_faults = {}
    # list to remember the order of lines in the file
    line_order = []

    with open(filepath, 'r') as file:
        lines = file.readlines()

    current_gate = None
    inputs_to_read = 0

    for line in lines:
        clean_line = line.strip()
        
        # 1. Skip spaces and stars (comments and empty lines)
        if clean_line == "" or clean_line.startswith('*'):
            continue

        # 2. Split the line into separate parts and filter out fault tags (like '>sa1')
        raw_parts = clean_line.split()
        parts = []
        current_faults = []
        
        for part in raw_parts:
            # Check for stuck-at faults and save them
            if part == ">sa0":
                current_faults.append(0)
            elif part == ">sa1":
                current_faults.append(1)
                
            if not part.startswith('>'):
                parts.append(part)
        
        # If no useful data on this line, skip it
        if len(parts) == 0:
            continue

        if inputs_to_read > 0:
            for part in parts:
                # Only take pure numbers
                if part.isdigit():
                    input_id = int(part)
                    gate_input[current_gate].append(input_id)
                    inputs_to_read -= 1
                    if inputs_to_read == 0:
                        break
            continue

        if len(parts) < 3:
            continue
            
        line_id = int(parts[0])
        line_order.append(line_id)
        line_faults[line_id] = current_faults
        
        if "from" in clean_line.lower():
            last_word = parts[-1]
            source_id_str = ""
            for char in last_word:
                if char.isdigit():
                    source_id_str += char
            
            if source_id_str:
                wire_to_source[line_id] = int(source_id_str)
        else:
            if len(parts) < 5:
                continue
            gate_type = parts[2].upper()
            wire_to_source[line_id] = line_id
            if gate_type == 'INPT':
                gate_type = "PI"
                
            gates[line_id] = {
                "id": line_id,
                "type": gate_type,
                "fanouts": []
            }
            gate_input[line_id] = []
            fanin_counts = int(parts[4])
            if fanin_counts > 0:
                current_gate = line_id
                inputs_to_read = fanin_counts

    # use Tuple Unpacking
    for id, input in gate_input.items():
        for input_id in input:
            # find authetic source for fan in guard
            authetic_wire_to_source = wire_to_source.get(input_id, input_id)
            
            # If the source exists, add 'id' to its 'fanouts' list
            if authetic_wire_to_source in gates:
                if id not in gates[authetic_wire_to_source]["fanouts"]:
                    gates[authetic_wire_to_source]["fanouts"].append(id)

    result_list = []
    sorted_gate_ids = sorted(gates.keys())
    
    for gid in sorted_gate_ids:
        # Sort fanouts for cleaner JSON output
        gates[gid]["fanouts"].sort()
        result_list.append(gates[gid])
        
    # Generate the fault list output
    fault_list = []
    for current_line_id in line_order:
        faults_on_this_line = line_faults.get(current_line_id, [])
        
        if len(faults_on_this_line) == 0:
            continue
            
        if current_line_id in gates:
            for f in faults_on_this_line:
                fault_list.append(f"{current_line_id:>4}    0 {f}")
        else:
            source_gate = wire_to_source.get(current_line_id)
            dest_gate = None
            for gid, input_list in gate_input.items():
                if current_line_id in input_list:
                    dest_gate = gid
                    break
                    
            if dest_gate is not None and source_gate is not None:
                for f in faults_on_this_line:
                    fault_list.append(f"{dest_gate:>4} {source_gate:>4} {f}")
            
    return result_list, fault_list


if __name__ == "__main__":
    isc_files = glob.glob("data/*.isc")
    
    if len(isc_files) == 0:
        print("Error: No .isc files found in ../data/ directory. Please check your folder structure.")
    
    for input_file in isc_files:

        output_file = input_file.replace(".isc", ".json")
        fault_file = input_file.replace(".isc", "_faults.txt")
        print(f"Processing {input_file}...")
        
        try:
            parsed_data, fault_data = parser(input_file)
            
            with open(output_file, 'w') as json_file:
                json.dump(parsed_data, json_file, indent=4)
                
            with open(fault_file, 'w') as txt_file:
                for line in fault_data:
                    txt_file.write(line + "\n")
                    
            print(f"  Done! Saved to {output_file} and {fault_file}")
        except Exception as e:
            print(f"  Error processing {input_file}: {e}")