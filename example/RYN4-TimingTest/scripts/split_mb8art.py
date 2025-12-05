#!/usr/bin/env python3
"""
Automated file splitter for MB8ART library
Based on the successful RYN4 splitting pattern
"""

import re
import os
import sys
from pathlib import Path

class MB8ARTSplitter:
    def __init__(self, source_path):
        self.source_path = Path(source_path)
        self.source_dir = self.source_path.parent
        self.content = self.source_path.read_text()
        self.methods = []
        self.categorized = {}
        
    def categorize_method(self, method_name, return_type, full_signature):
        """Categorize a method based on RYN4 patterns adapted for MB8ART"""
        
        # IDeviceInstance methods
        if 'IDeviceInstance::' in return_type or method_name in [
            'initialize', 'getDeviceType', 'getStatus', 'getLastError',
            'performSelfTest', 'requestData', 'processData', 'getData',
            'waitForInitializationComplete', 'performAction', 'waitForData',
            'waitForInitialization', 'registerCallback', 'unregisterCallbacks',
            'setEventNotification'
        ]:
            return 'MB8ARTDevice.cpp'
        
        # Modbus/communication methods
        if any(x in method_name.lower() for x in ['modbus', 'response', 'handle', 'validate']) or \
           any(x in method_name for x in ['onAsync', 'readSensor', 'readAll', 'sendRequest']):
            return 'MB8ARTModbus.cpp'
        
        # State query methods
        if (method_name.startswith(('is', 'get', 'was', 'check')) and 
            not any(x in method_name for x in ['Event', 'Bit', 'Enum'])) or \
           method_name in ['printSensorStatus', 'printModuleSettings']:
            return 'MB8ARTState.cpp'
        
        # Configuration methods
        if any(x in method_name for x in ['set', 'req', 'initializeModule', 'Settings', 
                                          'BaudRate', 'Parity', 'Address', 'Factory', 
                                          'ToString', 'Enum', 'Stored']):
            return 'MB8ARTConfig.cpp'
        
        # Event methods
        if any(x in method_name for x in ['Event', 'Bit', 'notification']):
            return 'MB8ARTEvents.cpp'
        
        # Sensor-specific methods
        if any(x in method_name for x in ['Sensor', 'sensor', 'Mapping', 'mapping',
                                          'process', 'control', 'measure']):
            return 'MB8ARTSensor.cpp'
        
        # Keep in main file
        return 'MB8ART.cpp'
    
    def find_all_methods(self):
        """Find all method implementations in the source file"""
        # Pattern to match MB8ART method definitions
        pattern = r'((?:[\w:]+(?:\s*<[^>]+>)?(?:\s+|\s*\*\s*))?)(MB8ART::[\w]+)\s*\([^)]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\s*\{'
        
        for match in re.finditer(pattern, self.content):
            return_type = match.group(1).strip() if match.group(1) else ''
            full_method = match.group(2)
            method_name = full_method.replace('MB8ART::', '')
            
            target_file = self.categorize_method(method_name, return_type, match.group(0))
            
            self.methods.append({
                'name': method_name,
                'full_name': full_method,
                'return_type': return_type,
                'target': target_file,
                'start': match.start(),
                'signature_end': match.end()
            })
        
        # Sort by position
        self.methods.sort(key=lambda x: x['start'])
        
        # Group by target file
        for method in self.methods:
            target = method['target']
            if target not in self.categorized:
                self.categorized[target] = []
            self.categorized[target].append(method)
    
    def extract_method_implementation(self, method):
        """Extract complete method implementation including comments"""
        start = method['start']
        
        # Find preceding comments
        comment_start = start
        lines = self.content[:start].split('\n')
        for i in range(len(lines) - 1, -1, -1):
            line = lines[i].strip()
            if line.startswith('//') or line == '' or line.startswith('/*'):
                if i > 0:
                    comment_start = len('\n'.join(lines[:i]))
            else:
                break
        
        # Find matching closing brace
        brace_count = 1
        pos = method['signature_end']
        
        while pos < len(self.content) and brace_count > 0:
            if self.content[pos] == '{':
                brace_count += 1
            elif self.content[pos] == '}':
                brace_count -= 1
            pos += 1
        
        if brace_count == 0:
            # Include the newline after the closing brace
            while pos < len(self.content) and self.content[pos] in '\n':
                pos += 1
            return self.content[comment_start:pos]
        
        return None
    
    def create_file_header(self, filename, description):
        """Create standard file header"""
        return f'''/**
 * @file {filename}
 * @brief {description}
 * 
 * This file contains {description.lower()} for the MB8ART library.
 */

#include "MB8ART.h"
#include <MutexGuard.h>

'''
    
    def create_split_files(self):
        """Create the new split files"""
        file_descriptions = {
            'MB8ARTDevice.cpp': 'IDeviceInstance interface implementation',
            'MB8ARTModbus.cpp': 'Modbus communication and response handling',
            'MB8ARTState.cpp': 'State query and status methods',
            'MB8ARTConfig.cpp': 'Configuration and settings management',
            'MB8ARTEvents.cpp': 'Event group and notification handling',
            'MB8ARTSensor.cpp': 'Sensor-specific operations and mappings'
        }
        
        created_files = []
        
        for filename, methods in self.categorized.items():
            if filename == 'MB8ART.cpp':
                continue
                
            filepath = self.source_dir / filename
            
            # Create file content
            content = self.create_file_header(filename, file_descriptions.get(filename, 'Implementation'))
            
            # Add any specific includes based on file type
            if 'Config' in filename:
                content += '#include <RetryPolicy.h>\n'
            if 'Device' in filename:
                content += '#include <string.h>\n'
            if 'Sensor' in filename:
                content += '#include <algorithm>\n'
                
            content += '\nusing namespace mb8art;\n\n'
            
            # Add static definitions if needed
            if filename == 'MB8ARTDevice.cpp':
                content += '''// Static member definitions
IDeviceInstance::DataResult MB8ART::cachedSensorResult;
TickType_t MB8ART::cacheTimestamp;
const TickType_t MB8ART::CACHE_VALIDITY = pdMS_TO_TICKS(1000); // 1 second cache validity

'''
            
            # Add method implementations
            print(f"\nCreating {filename} with {len(methods)} methods:")
            for method in methods:
                print(f"  - {method['name']}()")
                impl = self.extract_method_implementation(method)
                if impl:
                    content += impl + '\n'
            
            # Write file
            filepath.write_text(content)
            created_files.append(filename)
            
        return created_files
    
    def update_library_json(self):
        """Update library.json with new source files"""
        lib_json_path = self.source_dir.parent / 'library.json'
        
        if lib_json_path.exists():
            content = lib_json_path.read_text()
            
            # Find srcFilter section
            if '"srcFilter"' in content:
                # Replace existing srcFilter
                new_filter = '''  "srcFilter": [
    "+<MB8ART.cpp>",
    "+<MB8ARTDevice.cpp>",
    "+<MB8ARTModbus.cpp>",
    "+<MB8ARTState.cpp>",
    "+<MB8ARTConfig.cpp>",
    "+<MB8ARTSensor.cpp>",
    "+<MB8ARTEvents.cpp>"
  ]'''
                content = re.sub(r'"srcFilter":\s*\[[^\]]*\]', new_filter, content, flags=re.DOTALL)
            else:
                print("Warning: srcFilter not found in library.json - please add manually")
            
            lib_json_path.write_text(content)
            print("\nUpdated library.json with new source files")
    
    def generate_removal_script(self):
        """Generate a script to remove moved methods from original file"""
        script_path = self.source_dir / 'remove_moved_methods.py'
        
        moved_methods = []
        for filename, methods in self.categorized.items():
            if filename != 'MB8ART.cpp':
                moved_methods.extend([m['name'] for m in methods])
        
        script_content = f'''#!/usr/bin/env python3
"""Remove methods that have been moved to split files"""

import re

moved_methods = {moved_methods}

def remove_method(content, method_name):
    """Remove a method implementation from content"""
    pattern = rf'((?:[\\w:]+(?:\\s*<[^>]+>)?(?:\\s+|\\s*\\*\\s*))?)(MB8ART::{{re.escape(method_name)}})\s*\\([^{{]]*\\)\\s*(?:const\\s*)?(?:noexcept\\s*)?(?:override\\s*)?\\s*\\{{'
    
    matches = list(re.finditer(pattern, content))
    
    for match in reversed(matches):
        start = match.start()
        
        # Find preceding comments
        comment_start = start
        while comment_start > 0:
            line_start = content.rfind('\\n', 0, comment_start - 1)
            if line_start == -1:
                line_start = 0
            else:
                line_start += 1
            
            line = content[line_start:comment_start].strip()
            if line.startswith('//') or line == '':
                comment_start = line_start
            else:
                break
        
        # Find matching closing brace
        brace_count = 1
        pos = match.end()
        
        while pos < len(content) and brace_count > 0:
            if content[pos] == '{{':
                brace_count += 1
            elif content[pos] == '}}':
                brace_count -= 1
            pos += 1
        
        if brace_count == 0:
            # Remove extra newlines
            while pos < len(content) and content[pos] in '\\n\\r':
                pos += 1
            content = content[:comment_start] + content[pos:]
    
    return content

# Read original file
with open('src/MB8ART.cpp', 'r') as f:
    content = f.read()

# Remove each method
for method in moved_methods:
    content = remove_method(content, method)
    print(f"Removed: {{method}}")

# Clean up multiple blank lines
content = re.sub(r'\\n\\n\\n+', '\\n\\n', content)

# Write back
with open('src/MB8ART.cpp', 'w') as f:
    f.write(content)

print(f"\\nRemoved {{len(moved_methods)}} methods from MB8ART.cpp")
'''
        
        script_path.write_text(script_content)
        script_path.chmod(0o755)
        print(f"\nGenerated removal script: {script_path}")
        
    def print_summary(self):
        """Print summary of the split"""
        print("\n" + "="*60)
        print("MB8ART File Splitting Summary")
        print("="*60)
        
        total_methods = len(self.methods)
        print(f"\nTotal methods found: {total_methods}")
        
        print("\nMethod distribution:")
        for filename, methods in sorted(self.categorized.items()):
            print(f"\n{filename} ({len(methods)} methods):")
            # Show first 5 methods as examples
            for method in methods[:5]:
                print(f"  - {method['name']}()")
            if len(methods) > 5:
                print(f"  ... and {len(methods) - 5} more")

def main():
    if len(sys.argv) != 2:
        print("Usage: python split_mb8art.py path/to/MB8ART.cpp")
        sys.exit(1)
    
    source_file = sys.argv[1]
    
    if not os.path.exists(source_file):
        print(f"Error: {source_file} not found")
        sys.exit(1)
    
    print(f"Analyzing {source_file}...")
    
    splitter = MB8ARTSplitter(source_file)
    
    # Find all methods
    splitter.find_all_methods()
    
    # Print summary
    splitter.print_summary()
    
    # Ask for confirmation
    response = input("\nProceed with file splitting? (y/n): ")
    if response.lower() != 'y':
        print("Aborted.")
        return
    
    # Create backup
    backup_path = Path(source_file + '.backup')
    backup_path.write_text(splitter.content)
    print(f"\nCreated backup: {backup_path}")
    
    # Create split files
    created = splitter.create_split_files()
    print(f"\nCreated {len(created)} new files")
    
    # Update library.json
    splitter.update_library_json()
    
    # Generate removal script
    splitter.generate_removal_script()
    
    print("\n" + "="*60)
    print("NEXT STEPS:")
    print("1. Review the generated files")
    print("2. Test compilation: pio run")
    print("3. Fix any compilation errors")
    print("4. Run: python src/remove_moved_methods.py")
    print("5. Test again and commit changes")
    print("="*60)

if __name__ == '__main__':
    main()