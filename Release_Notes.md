


# Release Notes for VERISILICON Video Encoder Wrapper Layer
Copyright &copy; 2024 STMicroelectronics\

[![ST logo](_htmresc/st_logo_2020.png)](https://www.st.com)

# Purpose

The VERISILICON encoder wrapper layer is the software layer used to interface the VERISILICON video encoder software stack with the Video Encoder hardware peripheral.



# Update History

<label for="collapse-section5" aria-hidden="true">__V1.2.2 / 03-June-2026__</label>
<div>

## Main Changes

- Correct assert() in EWLInit() to allow H.264 and JPEG client parameters
- Add SW_Security_Level.md file
  
## Known limitations

- None

## Supported Devices and boards

- All boards embedding the Video Encoder hardware peripheral

## Backward compatibility

NA

## Dependencies

NA

</div>



<label for="collapse-section4" aria-hidden="true">__V1.2.1 / 16-September-2025__</label>
<div>

## Main Changes

- Add README.md and LICENSE.md files
  
## Known limitations

- None

## Supported Devices and boards

- All boards embedding the Video Encoder hardware peripheral

## Backward compatibility

NA

## Dependencies

NA

</div>



<label for="collapse-section3" aria-hidden="true">__V1.2.0 / 24-April-2025__</label>
<div>

## Main Changes

###  Fix portYIELD_FROM_ISR() call conditions

## Contents

Ensure portYIELD_FROM_ISR() is called regardless of error status
  
## Known limitations

- None

## Supported Devices and boards

- All boards embedding the Video Encoder hardware peripheral

## Backward compatibility

NA

## Dependencies

NA

</div>



<label for="collapse-section2" aria-hidden="true">__V1.1.0 / 05-February-2025__</label>
<div>

## Main Changes

###  Fix memory leak in de-initialization process

## Contents

Fix memory leak issue in EWLRelease() API when EWL_ALLOC_API is set to EWL_USE_FREERTOS_MM
  
## Known limitations

- None

## Supported Devices and boards

- All boards embedding the Video Encoder hardware peripheral

## Backward compatibility

NA

## Dependencies

NA

</div>




<label for="collapse-section1" aria-hidden="true">__V1.0.0 / 30-October-2024__</label>
<div>

## Main Changes

###  First official release


## Contents

VERISILICON Encoder Wrapper Layer official release.

  
## Known limitations

- None

## Supported Devices and boards

- All boards embedding the Video Encoder hardware peripheral

## Backward compatibility

NA

## Dependencies

NA

</div>





For complete documentation on STM32,visit: http://www.st.com)]

This release note uses up to date web standards and, for this reason, should not be opened with Internet Explorer
but preferably with popular browsers such as Google Chrome, Mozilla Firefox, Opera or Microsoft Edge.