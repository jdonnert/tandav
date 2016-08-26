; Cosmological distances & data (Hogg 1999)

pro CosmologyObject__define

    void = {CosmologyObject, $; Cosmological Model
                name        : '', $         ; Name of model
                t0          : double(0), $  ; Age of the universe
                H0          : double(0), $  ; Hubble constant
                Omega_b     : double(0), $  ; Baryon density
                Omega_c     : double(0), $  ; Dark matter density
                Omega_L     : double(0), $  ; Dark energy density
                Omega_r     : double(0), $  ; Photon energy density (CMB)
                sigma_8     : double(0), $  ; Fluctuation amplitude at 8/h Mpc
                Delta_r2    : double(0), $  ; Curvature fluctuation amplitude
                n_s         : double(0), $  ; scalar spectral index
                tau         : double(0), $  ; reionisation optical depth
                w           : double(0), $  ; equation of state parameter
                z_star      : double(0), $  ; redshift of decoupling
                t_star      : double(0), $  ; Age of decoupling
                z_reion     : double(0), $  ; Redshift of reionisation
                						 $  ; DERIVED QUANTITIES 
                Omega_M     : double(0), $  ; Matter density
                Omega_k     : double(0), $  ; curvature density
                Omega_tot   : double(0), $  ; total density
                hbpar       : double(0), $  ; h parameter
                H100        : double(0), $  ; hubble 100 constant
                d_hubble    : double(0), $  ; hubble distance
                t_hubble    : double(0), $  ; hubble time 
                inherits IDL_Object $       ; for public variables
            }
    return
end

function CosmologyObject::INIT  
        
    self.set, 0, /show_methods

    return, 1
end

; expose object variables
pro CosmologyObject::GetProperty, t0=t0, H0=H0, Omega_b=Omega_b, $
    Omega_c=Omega_c, Omega_L=Omega_L, Omega_r=Omega_r, sigma_8=sigma_8, $ 
	Delta_r2=Delta_r2, n_s=n_s, tau=tau, w=w, z_star=z_star, $
	t_star=t_star, z_reion=z_reion, Omega_M=Omega_M , Omega_k=Omega_k, $
	Omega_tot=Omega_tot, hbpar=hbpar, H100=H100, d_hubble=d_hubble, $
	t_hubble=t_hubble, name=name

    if arg_present(name)        then name = name
    if arg_present(t0)          then t0 = self.t0
    if arg_present(H0)          then H0 = self.H0
    if arg_present(Omega_b)     then Omega_b = self.Omega_b
    if arg_present(Omega_c)     then Omega_c = self.Omega_c
    if arg_present(Omega_L)     then Omega_L = self.Omega_L
    if arg_present(Omega_r)     then Omega_r = self.Omega_r
    if arg_present(sigma_8)     then sigma_8 = self.sigma_8
    if arg_present(Delta_r)    then Delta_r = self.Delta_r
    if arg_present(n_s)         then n_s = self.n_s
    if arg_present(tau)         then tau = self.tau
    if arg_present(w)           then w = self.w
    if arg_present(z_star)      then z_star = self.z_star
    if arg_present(t_star)      then t_star = self.t_star
    if arg_present(z_reion)     then z_reion = self.z_reion
    ; DERIVED QUANTITIES
    if arg_present(Omega_M)     then Omega_M = self.Omega_M
    if arg_present(Omega_k)     then Omega_k = self.Omega_k
    if arg_present(Omega_tot)   then Omega_tot = self.Omega_tot
    if arg_present(hbpar)       then hbpar = self.hbpar
    if arg_present(H100)        then H100 = self.H100
    if arg_present(d_hubble)    then d_hubble = self.d_hubble
    if arg_present(t_hubble)    then t_hubble = self.t_hubble

    return
end

; show current values
pro CosmologyObject::Show, show_methods=show_methods

    @set_cgs

    print, 'Cosmology : '+self.name
    print, '    H0      = '+strn(self.H0/3.2407765d-20, len=4)+' km/s/Mpc'
    print, '    Omega_L = '+strn(self.Omega_L, len=4)
    print, '    Omega_M = '+strn(self.Omega_M, len=4)
    print, '    Omega_b = '+strn(self.Omega_b, len=4)
    print, '    Omega_r = '+strn(self.Omega_r, len=4)
    print, '    sigma_8 = '+strn(self.sigma_8, len=4)
    print, '    Horizon = '+strn(self.d_hubble/(1000*kpc2cm), len=6)+' Mpc'
	
	if keyword_set(show_methods) then begin
		print, ' ' 
		print, "Methods (all cgs) :"
		print, '  scale factor at age t :           Cosmo.a(t)'
		print, '  Hubble paramter:                  H(z)'
		print, '  Comoving Distance:                d_comov(z)'
		print, '  Transverse Comoving Distance :    d_transcomov(z)'
		print, '  Angular Diameter Distance :       d_ang(z)'
		print, '  Luminosity Distance :             d_lum(z)'
		print, '  Critical Density :                rho_crit(z)'
		print, '  Overdensity Parameter :           Delta(z)'
		print, '  Redshift to time :                z2t(z)'
		print, '                                    t2z(t)'
		print, '  On the Sky :                      arcmin2kpc(arcmin, z)'
		print, '                                    kpc2arcmin(kpc, z)'
		print, '  Luminosity to flux :              lum2flux(Lum, z)'
		print, '                                    flux2lum(flux, z)'
	end else begin
		print, ' ' 
		print, '(see methods with cosmo.show, /show_methods'
	end

	print, ' ' 

    return
end

;set different literature values
pro CosmologyObject::Set, val, silent=silent, show_methods=show_methods

    @set_cgs

    if n_params() lt 1 then begin
        print, 'List of Cosmologies : '
        print, '    0 - H0=70, Omega_M=0.27, Omega_L=0.73'
        print, '    1 - H0=70, Omega_M=0.27, Omega_L=0.7'
        return
    end

    case val of 
        0 : begin
                self.name       = 'NONE'
                self.H0         = 1
                self.Omega_L    = 1                       
                self.Omega_b    = 1                       
                self.sigma_8    = 1                       
                self.Omega_M    = 1
                self.Omega_r    = 1
            end
        1 : begin
                self.name       = 'Concordance'
                self.H0         = 70*3.2407765d-20          ; 1/s
                self.Omega_L    = 0.7D                       
                self.Omega_b    = 0.04D                       
                self.sigma_8    = 0.8D                       
                self.Omega_M    = 0.3D 
                self.Omega_r    = 0
            end
        2 : begin   
                self.name       = "Other Concordance"
                self.H0         = 70*3.2407765d-20
                self.Omega_M    = 0.27D
                self.sigma_8    = 0.8D                       
                self.Omega_L    = 0.73D
                self.Omega_r    = 0
            end
    endcase

	RESOLVE_ALL, class='CosmologyObject', /continue_on_error

    self.H100 = 100D*3.2407765e-20 ; in cgs
    self.hbpar = self.H0/self.H100
    self.d_hubble = hubble_distance(self.H0)
    self.t_hubble = hubble_time(self.H0)
    self.Omega_tot = self.Omega_M+self.Omega_L+self.Omega_r
    self.Omega_k = curvature_density(self.Omega_M, self.Omega_L)
    
    if not keyword_set(silent) then $
        self.show, show_methods=show_methods
    
    return
end

; Provide short name bindings to more complicated functions as methods
; Each function has its own file in the class directory and takes all
; variables it needs as seperate inputs. Feel free to extend !

function CosmologyObject::a, t
    return, scale_factor(t, self)
end

function CosmologyObject::H, z
    return, hubble_parameter(z, self.H0, self.Omega_M, self.Omega_r, self.Omega_L)
end

function CosmologyObject::d_comov, z
    return, comoving_distance(z, self.H0, self.Omega_M, self.Omega_L)
end

function CosmologyObject::d_transcomov, z
    return, transverse_comoving_distance(z, self.H0, self.Omega_M, self.Omega_L)
end

function CosmologyObject::d_ang, z
    return, angular_diameter_distance(z, self.H0, self.Omega_M, self.Omega_L)
end

function CosmologyObject::d_lum, z
    return, luminosity_distance(z, self.H0, self.Omega_M, self.Omega_L)
end

function CosmologyObject::z2t, z
    return, lookback_time(z, self.H0, self.Omega_M, self.Omega_L)
end 

function CosmologyObject::t2z, t
    return, lookback_time(t, self.H0, self.Omega_M, self.Omega_L)
end

function CosmologyObject::arcmin2kpc, arcmin, z
    return, arcmin2kpc(arcmin, z, self.H0, self.Omega_M, self.Omega_L)
end

function CosmologyObject::kpc2arcmin, kpc, z 
    return, arcmin2kpc(kpc, z, self.H0, self.Omega_M, self.Omega_L, inv=1)
end

function CosmologyObject::rho_crit, z 
    return, critical_density(z, self.Omega_tot, self.H100, self.hbpar, $
							    self.Omega_M, self.Omega_r, self.Omega_L)
end

function CosmologyObject::Delta, z
	return, overdensity_parameter(z, self.Omega_L, self.Omega_M)
end

function  CosmologyObject::lum2flux, L, z
	return, luminosity2flux(L, z, self.H0, self.Omega_M, self.Omega_L)
end

function  CosmologyObject::flux2lum, f, z
	return, flux2luminosity(f, z, self.H0, self.Omega_M, self.Omega_L)
end
