using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Box : MonoBehaviour
{
    private BoxCollider2D boxCollider;
    [SerializeField] private Animator anim;
    [SerializeField] private GameObject box;


    // Start is called before the first frame update
    void Start()
    {
         boxCollider = GetComponent<BoxCollider2D>();
    }

    private void OnTriggerEnter2D(Collider2D collision)
 {
     if(collision.tag == "Attack")
     {       
        anim.SetTrigger("Broke");
        StartCoroutine(DestroyCoroutine());
     }
 }

 private IEnumerator DestroyCoroutine()
    {
       
        yield return new WaitForSeconds(0.5f);        
        Destroy(box);

    }    

}
