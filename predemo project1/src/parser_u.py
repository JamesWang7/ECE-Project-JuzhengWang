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
        for part in raw_parts:
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
                    
                    # Stop if we have collected all needed inputs
                    if inputs_to_read == 0:
                        break
            continue # Move to the next line of the file

        if len(parts) < 3:
            continue

        # assign first char to line_id
        line_id = int(parts[0])

        # Check if this line is a "Branch/Fanout"
        if "from" in clean_line.lower():
            # Get the last part and keep only digits
            last_part = parts[-1]
            source_id_str = ""
            for char in last_part:
                if char.isdigit():
                    source_id_str += char
            
            if source_id_str:
                source_id = int(source_id_str)
                wire_to_source[line_id] = source_id
        
        # This is a real Logic Gate or Primary Input
        else:
            if len(parts) < 5:
                continue
                
            gate_type = parts[2].upper()
            
            # source of a gate is itself
            wire_to_source[line_id] = line_id
            
            if gate_type == "INPT":
                # change INPT to PI
                gate_type = "PI"
            
            # gate information
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
            # find authetic source for fan in
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
            
    return result_list


if __name__ == "__main__":
    isc_files = glob.glob("../data/*.isc")

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
                    
            print(f"  Success! Saved {output_file} ({len(parsed_data)} gates) and {fault_file} ({len(fault_data)} faults).")
            
        except Exception as e:
            print(f"  Error processing {input_file}: {e}")