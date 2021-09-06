==================================================
Display Color Pipeline: Proposed DRM Properties
==================================================

This is how a typical display color hardware pipeline looks like:
 +-------------------------------------------+
 |                RAM                        |
 |  +------+    +---------+    +---------+   |
 |  | FB 1 |    |  FB 2   |    | FB N    |   |
 |  +------+    +---------+    +---------+   |
 +-------------------------------------------+
       |  Plane Color Hardware Block |
 +--------------------------------------------+
 | +---v-----+   +---v-------+   +---v------+ |
 | | Plane A |   | Plane B   |   | Plane N  | |
 | | DeGamma |   | Degamma   |   | Degamma  | |
 | +---+-----+   +---+-------+   +---+------+ |
 |     |             |               |        |
 | +---v-----+   +---v-------+   +---v------+ |
 | |Plane A  |   | Plane B   |   | Plane N  | |
 | |CSC/CTM  |   | CSC/CTM   |   | CSC/CTM  | |
 | +---+-----+   +----+------+   +----+-----+ |
 |     |              |               |       |
 | +---v-----+   +----v------+   +----v-----+ |
 | | Plane A |   | Plane B   |   | Plane N  | |
 | | Gamma   |   | Gamma     |   | Gamma    | |
 | +---+-----+   +----+------+   +----+-----+ |
 |     |              |               |       |
 +--------------------------------------------+
+------v--------------v---------------v-------|
||                                           ||
||           Pipe Blender                    ||
+--------------------+------------------------+
|                    |                        |
|        +-----------v----------+             |
|        |  Pipe DeGamma        |             |
|        |                      |             |
|        +-----------+----------+             |
|                    |            Pipe Color  |
|        +-----------v----------+ Hardware    |
|        |  Pipe CSC/CTM        |             |
|        |                      |             |
|        +-----------+----------+             |
|                    |                        |
|        +-----------v----------+             |
|        |  Pipe Gamma          |             |
|        |                      |             |
|        +-----------+----------+             |
|                    |                        |
+---------------------------------------------+
                     |
                     v
               Pipe Output

Proposal is to have below properties for a plane:

* Plane Degamma or Pre-Curve:
	* This will be used to linearize the input framebuffer data.
	* It will apply the reverse of the color transfer function.
	* It can be a degamma curve or OETF for HDR.
	* This linear data can be further acted on by the following
	* color hardware blocks in the display hardware pipeline

UAPI Name: PLANE_DEGAMMA_MODE
Description: Enum property with values as blob_id's which advertizes the
	    possible degamma modes and lut ranges supported by the platform.
	    This  allows userspace to query and get the plane degamma color
	    caps and choose the appropriate degamma mode and create lut values
	    accordingly.

UAPI Name: PLANE_DEGAMMA_LUT
Description: Blob property which allows a userspace to provide LUT values
	     to apply degamma curve using the h/w plane degamma processing
	     engine, thereby making the content as linear for further color
	     processing. Userspace gets the size of LUT and precision etc
	     from PLANE_DEGAMA_MODE_PROPERTY
	
* Plane CTM
	* This is a Property to program the color transformation matrix.
	* This can be used to perform a color space conversion like
	* BT2020 to BT709 or BT601 etc.
	* This block is generally kept after the degamma unit so that
	* linear data can be fed to it for conversion.

UAPI Name: PLANE_CTM
Description: Blob property which allows a userspace to provide CTM coefficients
	     to do color space conversion or any other enhancement by doing a
	     matrix multiplication using the h/w CTM processing engine

* Plane Gamma or Post-Curve
	* This can be used to perform 2 operations:
		* non-lineralize the framebuffer data. Can be used for
		* non linear blending. It can be a gamma curve or EOTF
		* for HDR.
		* Perform Tone Mapping operation. This is an operation
		* done when blending is done with HDR and SDR content.

UAPI Name: PLANE_GAMMA_MODE
Description: Enum property with values as blob_id's which advertizes the
	    possible gamma modes and lut ranges supported by the platform.
	    This  allows userspace to query and get the plane gamma color
	    caps and choose the appropriate gamma mode and create lut values
	    accordingly.

UAPI Name: PLANE_GAMMA_LUT
Description: Blob property which allows a userspace to provide LUT values
	     to apply gamma curve or perform tone mapping using the h/w plane
	     gamma processing engine, thereby making the content as linear
	     for further color processing. Userspace gets the size of LUT and
	     precision etc from PLANE_GAMA_MODE_PROPERTY
	
This is part of one plane engine. Data from multiple planes will be
then fed to pipe where it will get blended. There is a similar set of
properties available at crtc level which acts on this blended data.

Below is a sample usecase:

  ┌────────────┐      ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
  │FB1         │      │Degamma Block│     │ CTM Matrix  │     │ Gamma Block │
  │            ├─────►│Linearize-   ├────►│ BT709 to    ├────►│ SDR to HDR  │
  │BT709 SDR   │      │BT709 inverse│     │ BT2020      │     │ Tone Mapping├────────┐
  └────────────┘      └─────────────┘     └─────────────┘     └─────────────┘        │
                                                                                     │
  ┌────────────┐      ┌─────────────┐     ┌─────────────┐     ┌─────────────┐        │
  │FB2         │      │Degamma Block│     │ CTM Matrix  │     │ Gamma Block │        │
  │            ├─────►│Linearize-   ├────►│ BT601 to    ├────►│ SDR to HDR  ├─────┐  │
  │BT601 SDR   │      │BT601 inverse│     │ BT2020      │     │ Tone Mapping│     │  │
  └────────────┘      └─────────────┘     └─────────────┘     └─────────────┘     │  │
                                                                                  │  │
  ┌────────────┐      ┌─────────────┐     ┌─────────────┐     ┌─────────────┐     │  │
  │FB3         │      │Degamma Block│     │ CTM Matrix  │     │ Gamma Block │     │  │
  │            ├─────►│Linearize-   ├────►│ NOP (Data in├────►│ NOP (Data in├───┐ │  │
  │BT2020 HDR  │      │HDR OETF     │     │ BT2020)     │     │ HDR)        │   │ │  │
  └────────────┘      └─────────────┘     └─────────────┘     └─────────────┘   │ │  │
                                                                                │ │  │
                                                                                │ │  │
                                                                                │ │  │
┌───────────────────────────────────────────────────────────────────────────────┴─┴──┘
│
│ ┌─────────────┐      ┌─────────────┐      ┌───────────────┐
│ │ CRTC Degamma│      │ CRTC CTM    │      │ CRTC Gamma    │
└─┤ Use to make ├─────►│ Use for any ├─────►│ Use for Tone  ├─────► TO Port
  │ data linear │      │ Color Space │      │ Mapping/apply │
  │ after blend │      │ Conversion  │      │ transfer func │
  └─────────────┘      └─────────────┘      └───────────────┘


This patch series adds properties for plane color features. It adds
properties for degamma used to linearize data and CSC used for gamut
conversion. It also includes Gamma support used to again non-linearize
data as per panel supported color space. These can be utilize by user
space to convert planes from one format to another, one color space to
another etc.

Userspace can take smart blending decisions and utilize these hardware
supported plane color features to get accurate color profile. The same
can help in consistent color quality from source to panel taking
advantage of advanced color features in hardware.

These patches add the property interfaces and enable helper functions.
This series adds Intel's XE_LPD hw specific plane gamma feature. We
can build up and add other platform/hardware specific implementation
on top of this series.

Credits: Special mention and credits to Ville Syrjala for coming up
with a design for this feature and inputs. This series is based on
his original design and idea.
