# Subscription Manager Tests

## Running the Tests via CMake

From the project root directory (`market_book/`):

```bash
# Create a build directory
mkdir -p build && cd build

# Configure and enable testing
cmake .. -DBUILD_TESTING=ON

# Build the test executable
cmake --build . --target subscription_manager_test

# Run the test
ctest --output-on-failure

# Or run directly
./subscription_manager_test
```
